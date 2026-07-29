#include "Grid.h"
#include "Logger.h"

AIEPLACE_NAMESPACE_BEGIN

using namespace tabulate; // table types, scoped to this .cpp (not leaked via Logger.h)

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
    for( int x_index = 0; x_index < m_bins_per_row; x_index++)
        for( int y_index = 0; y_index < m_bins_per_col; y_index++)
            m_bins[x_index][y_index].iterationReset();
}

/**
 * @brief Distribute a node's area across all grid bins it overlaps.
 *
 * For each bin the node's bounding box intersects, compute the rectangular
 * intersection area and add it to that bin's total_overlap. Also records the
 * per-bin overlap on the node (for electrostatic force computation).
 *
 * Small nodes that fit entirely within one bin get the fast path (no
 * intersection math — area goes directly to the single bin). Large nodes
 * that span multiple bins get the exact intersection area for each bin.
 *
 * @param node_p Pointer to the node whose overlap to distribute.
 */
void Grid::computeBinOverlaps(Node* node_p)
{
    float w = node_p->getXsize();
    float h = node_p->getYsize();

    // Footprint: exact, or (when clamping) inflated to at least sqrt(2) bins per dimension
    // with an area-conserving weight = real_area / clamped_area (XPlace's expand_ratio).
    // Clamping smooths sub-bin cells to the grid resolution, so the density field — and hence
    // the electrostatic force/gradient the optimizer follows — has no sub-bin spikes. Macros
    // already exceed the clamp, so they are unaffected (weight stays 1). Must stay in sync with
    // Placer::computeOverflow, which applies the same clamp to the smoothed overflow metric.
    float cw = w, ch = h, weight = 1.0f;
    if (m_clamp_density) {
        const float SQRT2 = 1.41421356f;
        cw = std::max(w, m_bin_width  * SQRT2);
        ch = std::max(h, m_bin_height * SQRT2);
        weight = (cw > 0.0f && ch > 0.0f) ? (w * h) / (cw * ch) : 0.0f;
    }

    // Footprint centered on the cell. Movable cells are shifted to stay inside the die so edge
    // cells still deposit their full area-conserving mass (matches XPlace pre_normalize clamping).
    // FIXED terminals are NOT shifted: IO pads/blockages sit in the margin outside the core-row
    // die, and shifting would pile their full area onto the edge bins (a false density moat that
    // repels cells/fillers). Instead they are geometrically clipped to the die below (XPlace drops
    // out-of-die fixed density from init_density_map).
    float grid_w = m_bins_per_row * m_bin_width;
    float grid_h = m_bins_per_col * m_bin_height;
    float node_xl = node_p->getProbeX() + 0.5f * w - 0.5f * cw;
    float node_yl = node_p->getProbeY() + 0.5f * h - 0.5f * ch;
    if (node_p->getStatus() != FIXED) {
        if (node_xl + cw > grid_w) node_xl = grid_w - cw;
        if (node_yl + ch > grid_h) node_yl = grid_h - ch;
        if (node_xl < 0.0f) node_xl = 0.0f;
        if (node_yl < 0.0f) node_yl = 0.0f;
    }
    float node_xh = node_xl + cw;
    float node_yh = node_yl + ch;

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
            bin.total_overlap += overlap_area;
            node_p->addBinOverlap(&bin, overlap_area);
        }
    }
}

/**
 * @brief Clamp each bin's overlap to target_density * bin_area after fixed components are added,
 *        so fixed macros don't count as overflow — only movable cells stacked on top do.
 */
void Grid::clampFixedDensity(float target_density)
{
    for (int col = 0; col < m_bins_per_row; col++)
        for (int row = 0; row < m_bins_per_col; row++) {
            float cap = m_bins[col][row].bb.getArea() * target_density;
            m_bins[col][row].total_overlap = std::min(m_bins[col][row].total_overlap, cap);
        }
}

std::vector< std::vector<float> > Grid::getBinDensities()
{
    // Compute per-bin density (rho = total_overlap / bin_area)
    std::vector< std::vector<float> > density;
    float bin_area_inv = 1 / m_bins[0][0].bb.getArea();

    for (int col = 0; col < m_bins_per_row; col++)
    {
        density.push_back(std::vector<float>(m_bins_per_col));
        for (int row = 0; row < m_bins_per_col; row++)
        {
            density[col][row] = m_bins[col][row].total_overlap * bin_area_inv;
        }
    }
    return density;
}

std::vector< std::vector<float> > Grid::get_a_uv()
{
    std::vector< std::vector<float> > a_uv;

    for (int col = 0; col < m_bins_per_row; col++)
    {
        a_uv.push_back(std::vector<float>(m_bins_per_col));
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
            cout << "Bin["<<x_index<<"]["<<y_index<<"]\tEx: " << b.eField.x << "\tEy: " << b.eField.y << endl;
        }
}
AIEPLACE_NAMESPACE_END