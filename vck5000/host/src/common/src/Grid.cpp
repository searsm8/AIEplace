#include "Grid.h"
#include "Logger.h"

AIEPLACE_NAMESPACE_BEGIN

using namespace tabulate; // table types, scoped to this .cpp (not leaked via Logger.h)

/// @brief Density footprint geometry — see the contract in Grid.h.
NodeFootprint computeNodeFootprint(Node* node_p, const FootprintConfig& cfg)
{
    const float w = node_p->getXsize();
    const float h = node_p->getYsize();

    float cw = w, ch = h, weight = 1.0f;
    if (cfg.clamp) {
        cw = std::max(w, cfg.bin_w * (float)M_SQRT2);
        ch = std::max(h, cfg.bin_h * (float)M_SQRT2);
        weight = (cw > 0.0f && ch > 0.0f) ? (w * h) / (cw * ch) : 0.0f;

        // XPlace OVERWRITES the area-conserving ratio for movable macros with target_density when
        // target_density < 1.0 (database.py:921-923) — it replaces the ratio, it does not scale
        // it (TODO #11b). Fillers are excluded there (the masked_fill spans only
        // [mov_lhs,mov_rhs)); isMovableMacro() is false for fillers and FIXED nodes here, so they
        // are excluded too.
        //
        // Deliberately INSIDE the clamp branch: like the sqrt(2) inflation, this is part of the
        // SMOOTHED density model the optimizer minimizes, which is the only density map XPlace has.
        // computeOverflow(clamp=false) is a sw_only-only diagnostic meant to be the *physical*
        // density — letting macros deposit 0.8x their real area there would deflate the metric and
        // make it incomparable to XPlace's own "exact" overflow.
        if (cfg.target_density < 1.0f && node_p->isMovableMacro())
            weight = cfg.target_density;
    }

    // No in-die correction here: enforceDieBoundaries already projects every movable node's
    // node_pos AND probe_pos using this same expanded size, so the footprint is legal by
    // construction (TODO #11a). Fixed nodes are clipped geometrically by the caller.
    float xl = node_p->getProbeX() + 0.5f * w - 0.5f * cw;
    float yl = node_p->getProbeY() + 0.5f * h - 0.5f * ch;

    return NodeFootprint{xl, yl, xl + cw, yl + ch, weight};
}

/// @brief Build the bins_per_row x bins_per_col grid and seed each bin's local density weight.
void Grid::init()
{
    m_bin_width  = m_die_area.getXsize() / (float)m_bins_per_row;
    m_bin_height = m_die_area.getYsize() / (float)m_bins_per_col;

    for( int x_index = 0; x_index < m_bins_per_row; x_index++)
    {
        m_bins.push_back(std::vector<Bin>());
        for( int y_index = 0; y_index < m_bins_per_col; y_index++)
        {
            Bin b = Bin(x_index*m_bin_width, y_index*m_bin_height, 
                   (x_index+1)*m_bin_width, (y_index+1)*m_bin_height);
            b.local_density_weight = INITIAL_LOCAL_DENSITY_WEIGHT;
            m_bins[x_index].push_back(b);
        }
    }
}

/// @brief Clear every bin's per-iteration accumulators (overlap, a_uv, field) before the next solve.
void Grid::iterationReset()
{
    #pragma omp parallel for schedule(static)
    for( int x_index = 0; x_index < m_bins_per_row; x_index++)
        for( int y_index = 0; y_index < m_bins_per_col; y_index++)
            m_bins[x_index][y_index].iterationReset();
}

/**
 * @brief Record how a node's area splits across the grid bins its density footprint overlaps.
 *
 * Always appends the per-bin intersection areas to the node's OWN BinOverlap list — private to
 * the node, and in an order fixed by this loop, so the electrostatic force gather that later
 * walks it sums identically every run. This half is all of the geometry and most of the cost,
 * and is safe to thread under either reduction policy.
 *
 * @param deposit_atomically also add each area into the SHARED bin, under an atomic. That is
 *        the reduction, so it is fused in here only for the !g_deterministic path, where it
 *        saves a second pass over every node. The ordered path leaves it to
 *        depositNodeOverlaps() instead.
 */
void Grid::computeNodeOverlaps(Node* node_p, bool deposit_atomically)
{
    NodeFootprint fp = computeNodeFootprint(node_p, footprintConfig());
    float node_xl = fp.xl, node_yl = fp.yl;
    float node_xh = fp.xh, node_yh = fp.yh;
    float weight  = fp.weight;

    int col_lo = std::max(0, (int)(node_xl / m_bin_width));
    int col_hi = std::min(m_bins_per_row - 1, (int)(node_xh / m_bin_width));
    int row_lo = std::max(0, (int)(node_yl / m_bin_height));
    int row_hi = std::min(m_bins_per_col - 1, (int)(node_yh / m_bin_height));

    for (int col = col_lo; col <= col_hi; col++) {
        float bin_xl = col * m_bin_width;
        float overlap_w = std::min(node_xh, bin_xl + m_bin_width) - std::max(node_xl, bin_xl);
        if (overlap_w <= 0.0f) continue;

        for (int row = row_lo; row <= row_hi; row++) {
            float bin_yl = row * m_bin_height;
            float overlap_h = std::min(node_yh, bin_yl + m_bin_height) - std::max(node_yl, bin_yl);
            if (overlap_h <= 0.0f) continue;

            float overlap_area = overlap_w * overlap_h * weight;  // area-conserving deposit
            Bin& bin = m_bins[col][row];
            if (deposit_atomically) {
                #pragma omp atomic
                bin.total_overlap += overlap_area;
            }
            node_p->addBinOverlap(&bin, overlap_area);
        }
    }
}

/**
 * @brief Add a node's recorded overlaps into the shared bins — the scatter half of the density
 *        deposit, split out so it can be replayed on one thread in the original node order.
 *        That replay is what makes the bin totals bit-identical to the serial golden.
 */
void Grid::depositNodeOverlaps(Node* node_p)
{
    for (BinOverlap& bo : node_p->getBinOverlaps())
        bo.bin_p->total_overlap += bo.overlap;
}

/**
 * @brief Saturate each bin's fixed-component occupancy at FULL, then scale it by target_density,
 *        so fixed macros don't count as overflow — only movable cells stacked on top do.
 *
 * SCALE, not cap (TODO #3, fixed 2026-08-17). XPlace does
 * `init_density_map.clamp_(min=0.0, max=1.0).mul_(args.target_density)` (`initializer.py:82`) —
 * in density terms `min(rho, 1) * td`. We used to compute `min(rho, td)`, which is a different
 * function everywhere a bin is partially occupied: at td=0.65 and rho=0.5 XPlace gives 0.325 and
 * the cap gives 0.50, so we read HIGH in macro-perimeter bins. Identical at td=1, which is why
 * only macro designs at td<1 ever saw it, and why the fillerless std-cell designs (#31's fft_2
 * reconciliation, 0 fixed cells) could not.
 *
 * total_overlap is an absolute area, so `min(rho,1)*td` is `min(overlap, bin_area) * td`.
 */
void Grid::clampFixedDensity(float target_density)
{
    #pragma omp parallel for schedule(static)
    for (int col = 0; col < m_bins_per_row; col++)
        for (int row = 0; row < m_bins_per_col; row++) {
            float bin_area = m_bins[col][row].bb.getArea();
            float saturated = std::min(m_bins[col][row].total_overlap, bin_area);
            m_bins[col][row].total_overlap = saturated * target_density;
        }
}

std::vector< std::vector<float> > Grid::getBinDensities()
{
    // Compute per-bin density (rho = total_overlap / bin_area)
    std::vector< std::vector<float> > density(m_bins_per_row, std::vector<float>(m_bins_per_col));
    float bin_area_inv = 1 / m_bins[0][0].bb.getArea();

    #pragma omp parallel for schedule(static)
    for (int col = 0; col < m_bins_per_row; col++)
    {
        for (int row = 0; row < m_bins_per_col; row++)
        {
            density[col][row] = m_bins[col][row].total_overlap * bin_area_inv;
        }
    }
    return density;
}

std::vector< std::vector<float> > Grid::get_a_uv()
{
    std::vector< std::vector<float> > a_uv(m_bins_per_row, std::vector<float>(m_bins_per_col));

    #pragma omp parallel for schedule(static)
    for (int col = 0; col < m_bins_per_row; col++)
    {
        for (int row = 0; row < m_bins_per_col; row++)
        {
            a_uv[col][row] = m_bins[col][row].a_uv;
        }
    }
    return a_uv;
}

/**
 * @brief ePlace overflow metric: total bin area above capacity, normalized by movable area.
 * @param target_density   per-bin capacity fraction (bin_capacity = target_density * bin_area)
 * @param total_movable_area normalizing denominator (movable cell area)
 * @return overflow ratio in [0, ~1]; convergence target is ~0.07
 */
float Grid::computeTotalOverflow(float target_density, float total_movable_area)
{
    float overflow_area = 0;
    for (int col = 0; col < m_bins_per_row; col++)
        for (int row = 0; row < m_bins_per_col; row++) {
            float bin_capacity = m_bins[col][row].bb.getArea() * target_density;
            float excess = m_bins[col][row].total_overlap - bin_capacity;
            overflow_area += std::max(0.0f, excess);
        }
    return overflow_area / (total_movable_area + 1e-8f);
}

/*****************
 * Print Functions
*****************/
void Grid::printOverflows()
{
    Table overflows;
    overflows.add_row(RowStream{} << "loc index" << "area" << "overlap" << "overflow");
    for( int x_index = 0; x_index < m_bins_per_row; x_index++)
    {
        for( int y_index = 0; y_index < m_bins_per_col; y_index++)
        {
            Bin bin = m_bins[x_index][y_index];
            float overflow = bin.getOverflowRatio();
            float overlap= bin.total_overlap;
            if (overflow > 0)
            {
                overflows.add_row(RowStream{} << std::to_string(x_index) + ", " + std::to_string(y_index)
                        << bin.bb.getArea() << overlap << overflow);
            }
        }
    }
    Logger::log_detail(overflows);
}

void Grid::printElectricFields()
{
    for( int x_index = 0; x_index < m_bins_per_row; x_index++)
        for( int y_index = 0; y_index < m_bins_per_col; y_index++)
        {
            Bin b = m_bins[x_index][y_index];
            Logger::log_info("Bin[" + std::to_string(x_index) + "][" + std::to_string(y_index) +
                              "]\tEx: " + std::to_string(b.eField.x) + "\tEy: " + std::to_string(b.eField.y));
        }
}
AIEPLACE_NAMESPACE_END