// Density.cpp
// Electric field and density computation functions
// Separated from AIEplace.cpp for better organization

#include "AIEplace.h"
#include "DCT.h"
#include <cmath>
#include <algorithm>
#include <vector>

AIEPLACE_NAMESPACE_BEGIN

void Placer::computeElectricFields()
{
    computeOverlaps();                 // update the density ρ at probe positions

    if(density_method == "cpu") {
        computeElectricFields_DCT();   // Compute E-fields on CPU using DCT
        //computeElectricFields_CPU(); // Compute E-fields using naive algorithm
    } else {
        Logger::log_error("Invalid density_compute_method specified in config file "
                          "(sw_only supports 'cpu' only; AIE acceleration lives in pl_algo)");
        exit(1);
    }
}

/***************
 * CPU FUNCTIONS
 ****************/

/*
 * @brief On CPU, compute Electric fields using straightforward "naive" method
 *
**/
void Placer::computeElectricFields_CPU()
{
    //Logger::log_detail("Begin computeElectricFields_CPU()");
    compute_a_uv_naive();
    compute_eField_naive();
}

/// @brief Compute the per-bin electric field on the CPU via the 2D-DCT method (a_uv then E).
void Placer::computeElectricFields_DCT()
{
    TIME_FUNCTION();
    //Logger::log_detail("Begin computeElectricFields_DCT()");
    compute_a_uv_DCT();
    compute_eField_DCT();
}


// Compute the intermediate term a_uv for Efields
// Store results in each bin
// Implements DREAMplace Eq 3a
void Placer::compute_a_uv_naive()
{
    std::vector< std::vector<float> > density = grid.getBinDensities(); // rho
    for (int u = 0; u < grid.getBinsPerRow(); u++) {
        for (int v = 0; v < grid.getBinsPerCol(); v++) {
            //float w_u = 1 * M_PI * u / grid.getBinsPerRow();
            //float w_v = 1 * M_PI * v / grid.getBinsPerCol();
            float w_u = 2 * M_PI * u / grid.getBinsPerRow();
            float w_v = 2 * M_PI * v / grid.getBinsPerCol();

            // For each bin at (u, v) compute the intermediate term a
            float a_uv = 0;
            for (int x = 0; x < grid.getBinsPerRow(); x++) {
                for (int y = 0; y < grid.getBinsPerCol(); y++) {
                    a_uv += density[x][y] * cos(w_u*x) * cos(w_v*y);
                    //cout << "density (rho): " << density[x][y] << "\toverlap/bb.area: " << (grid.getBin(x, y).overlap / grid.getBin(x, y).bb.getArea()) << endl;
                    //a_uv += (grid.getBin(x, y).overlap / grid.getBin(x, y).bb.getArea()) * cos(w_u*x) * cos(w_v*y);
                }
            }
            //a_uv /= 2 * grid.getBinsPerRow(); // 1 / 2n
            a_uv /= grid.getBinsPerCol() * grid.getBinsPerRow(); // 1 / M^2
            grid.getBin(u, v).a_uv = a_uv;
        }
    }
}

// Compute the x and y components of electric fields
// Implements DREAMplace Eq 3c, 3d
void Placer::compute_eField_naive()
{
    for (int x = 0; x < grid.getBinsPerRow(); x++) {
        for (int y = 0; y < grid.getBinsPerCol(); y++) {
            float w = 2 * M_PI / grid.getBinsPerRow();
            // For each bin at (u, v) compute the intermediate term a
            float Ex = 0;
            float Ey = 0;
            for (int u = 0; u < grid.getBinsPerRow(); u++) {
                for (int v = 0; v < grid.getBinsPerCol(); v++) {
                    if ( u == 0 && v == 0) continue; // avoid division by 0
                    float w_u = w*u;
                    float w_v = w*v;
                    float a_uv = grid.getBin(u, v).a_uv;
                    Ex += (a_uv*w_u) / (w_u*w_u + w_v*w_v) * sin(w_u*x) * cos(w_v*y);
                    Ey += (a_uv*w_v) / (w_u*w_u + w_v*w_v) * cos(w_u*x) * sin(w_v*y);
                }
            }
            grid.getBin(x, y).eField.x = Ex;
            grid.getBin(x, y).eField.y = Ey;
        }
    }
}

/** @brief: Compute the intermediate term a_uv using DCTs*/
void Placer::compute_a_uv_DCT()
{
    TIME_FUNCTION();
    const int num_rows = grid.getBinsPerCol();
    const int num_cols = grid.getBinsPerRow();
    std::vector< std::vector<float> > density = grid.getBinDensities(); // rho
    std::vector< std::vector<float> > temp(num_rows, std::vector<float>(num_cols));
    std::vector< std::vector<float> > a_uv(num_rows, std::vector<float>(num_cols));

    // Perform 1-D DCT on rows of density (rho) matrix (FFT, O(N log N); verified == DCT_naive)
    // Every 1-D transform in a pass reads and writes only its own row, so threading the row
    // loop reorders nothing -- the result is bit-exact at any thread count.
    {   TIME_BLOCK("dct_rowpass");
        #pragma omp parallel for schedule(static)
        for (int row_index = 0; row_index < num_rows; row_index++)
            DCT_fft(density[row_index].data(), temp[row_index].data(), num_cols, dct_normalize);
    }

    {   TIME_BLOCK("dct_transpose"); temp = transpose(temp); }

    // Perform 1-D DCT on transposed matrix
    {   TIME_BLOCK("dct_rowpass");
        #pragma omp parallel for schedule(static)
        for (int col_index = 0; col_index < num_cols; col_index++)
            DCT_fft(temp[col_index].data(), a_uv[col_index].data(), num_rows, dct_normalize);
    }

    {   TIME_BLOCK("dct_transpose"); a_uv = transpose(a_uv); }

    {   TIME_BLOCK("dct_grid_io");
        #pragma omp parallel for schedule(static)
        for (int u = 0; u < num_cols; u++)
            for (int v = 0; v < num_rows; v++) {
               grid.getBin(u, v).a_uv = a_uv[u][v];
            }
    }
}

/** @brief: Compute the eField values using DCTs*/
void Placer::compute_eField_DCT()
{
    TIME_FUNCTION();
    int num_rows = grid.getBinsPerCol();
    int num_cols = grid.getBinsPerRow();
    std::vector< std::vector<float> > Ex     (num_rows, std::vector<float>(num_cols));
    std::vector< std::vector<float> > Ey     (num_rows, std::vector<float>(num_cols));
    std::vector< std::vector<float> > a_uv = grid.get_a_uv();

    float w = 2 * M_PI / num_cols;
    Ex[0][0] = 0; Ey[0][0] = 0;

    {   TIME_BLOCK("dct_spectral");
    #pragma omp parallel for schedule(static)
    for (int u = 0; u < num_rows; u++) {
        for (int v = 0; v < num_cols; v++) {
            if ( u == 0 && v == 0) continue; // avoid division by 0
            float w_u = 2*M_PI*u / num_cols;
            float w_v = 2*M_PI*v / num_rows;
            float denom = w_u*w_u + w_v*w_v;

            Ex[u][v] = a_uv[u][v] * w_u / denom;
            Ey[u][v] = a_uv[u][v] * w_v / denom;
        }
    }
    }

    // Inverse transforms are left UNNORMALIZED (default). The forward a_uv already carries the 1/N^2;
    // re-applying 1/N on the inverse would double-normalize — a spurious extra 1/N^2 vs the naive
    // DREAMPlace field that inflates lambda ~N^2 and corrupts the preconditioner (the distortion grows
    // with grid). Unnormalized = field-faithful: verified == compute_eField_naive and matches XPlace.
    // compute IDCT on all rows of Ex, and IDXST on all rows of Ey (FFT; verified == naive)
    {   TIME_BLOCK("dct_rowpass");     // transforms run in place (out may alias in)
        #pragma omp parallel for schedule(static)
        for (int row_index = 0; row_index < num_rows; row_index++) {
            IDCT_fft (Ex[row_index].data(), Ex[row_index].data(), num_cols);
            IDXST_fft(Ey[row_index].data(), Ey[row_index].data(), num_cols);
        }
    }

    {   TIME_BLOCK("dct_transpose"); Ex = transpose(Ex); Ey = transpose(Ey); }

    // compute IDCT on all rows of Ey, and IDXST on all rows of Ex
    {   TIME_BLOCK("dct_rowpass");
        #pragma omp parallel for schedule(static)
        for (int row_index = 0; row_index < num_rows; row_index++) {
            IDXST_fft(Ex[row_index].data(), Ex[row_index].data(), num_cols);
            IDCT_fft (Ey[row_index].data(), Ey[row_index].data(), num_cols);
        }
    }

    {   TIME_BLOCK("dct_transpose"); Ex = transpose(Ex); Ey = transpose(Ey); }

    // Put results in the grid bins
    {   TIME_BLOCK("dct_grid_io");
        #pragma omp parallel for schedule(static)
        for (int x = 0; x < num_cols; x++) {
            for (int y = 0; y < num_rows; y++) {
                grid.getBin(x, y).eField.x = Ex[x][y];
                grid.getBin(x, y).eField.y = Ey[x][y];
            }
        }
    }
}


/**
 * @brief Deposit every component's area into the bin grid to build the density map ρ.
 *        Two passes: fixed components first (clamped to a per-bin capacity baseline), then
 *        movable components and fillers, so only density stacked above fixed macros overflows.
 */
void Placer::computeOverlaps()
{
    TIME_FUNCTION();
    Logger::log_trace("Begin computeOverlaps()");
    const auto& fixed   = db.getFixedComponents();
    const auto& movable = db.getMovableComponents();
    const auto& fillers = db.getFillers();

    // The two passes are ordered and must stay so: clampFixedDensity reads the fixed baseline.
    // Within a pass the geometry is per-node and always threaded; only the scatter into shared
    // bins depends on the policy. Dynamic scheduling because a fixed macro covering thousands
    // of bins costs orders of magnitude more than a standard cell.
    auto deposit_pass = [&](const auto& node_vec) {
        #pragma omp parallel for schedule(dynamic, 512)
        for (int i = 0; i < (int)node_vec.size(); i++)
            grid.computeNodeOverlaps(node_vec[i], !g_deterministic);
        if (!g_deterministic) return;   // the deposit was fused into the pass above

        // Ordered deposit: one thread, node order, and within a node the order its own geometry
        // loop built the list -- so every bin sees exactly the sequence of adds the
        // single-threaded code performed. Costs a second pass over the nodes, which is why the
        // atomics path fuses instead.
        for (int i = 0; i < (int)node_vec.size(); i++)
            grid.depositNodeOverlaps(node_vec[i]);
    };

    // Pass 1: Fixed components — their density is clamped so bins fully covered
    // by fixed macros register as "at capacity" but not overflowed.
    deposit_pass(fixed);

    grid.clampFixedDensity(target_density);

    // Pass 2: Movable components and fillers — any density on top of
    // the clamped fixed baseline counts as real overflow.
    deposit_pass(movable);
    deposit_pass(fillers);
}

/**
 * @brief Overflow metric with fillers excluded. smooth=true gives the *smoothed* overflow
 * (the global-placement convergence signal; equivalent to XPlace's expand_ratio-inflated
 * density field); smooth=false gives the *exact* physical overflow.
 *
 * Same formula as Grid::computeTotalOverflow (sum of per-bin excess over target*bin_area,
 * normalized by movable area), evaluated on an independently-built density map so the two
 * variants can be reported side by side. When smooth=true each movable cell's footprint is
 * inflated to at least sqrt(2) bins per dimension with an area-conserving weight
 * (real_area/clamped_area) and shifted to stay in-die — matching Grid::computeBinOverlaps —
 * so sub-bin cells are smeared to grid resolution rather than spiking a single bin. Fixed
 * macros form a per-bin-capped baseline (mirrors clampFixedDensity); fillers are excluded.
 *
 * Why smoothed matters: it is the smoothed density the electrostatic optimizer actually
 * minimizes, so it descends cleanly to the stop threshold. The exact overflow re-measures
 * with sharp footprints, whose sub-bin quantization spikes leave it floored above threshold
 * even for a well-spread placement (the sw_only "can't reach 0.07" effect).
 */
float Placer::computeOverflow(bool smooth, std::vector<float>* out_density, bool include_fillers,
                              bool exclude_macros)
{
    TIME_FUNCTION();
    const int nx = grid.getBinsPerRow();
    const int ny = grid.getBinsPerCol();
    const float bin_w = grid.getBinWidth();
    const float bin_h = grid.getBinHeight();
    const float cap   = bin_w * bin_h * target_density;   // per-bin capacity

    std::vector<float> density(nx * ny, 0.0f);            // area deposited per bin

    // Deposit a node's area over its footprint. The geometry (sqrt(2) clamp, area-conserving
    // weight, in-die shift) comes from the shared computeNodeFootprint so this metric measures
    // exactly the density field Grid::computeBinOverlaps builds for the solver.
    auto deposit = [&](Node* node_p, bool clamp_node) {
        NodeFootprint fp = computeNodeFootprint(node_p, grid.footprintConfig(clamp_node));
        float xl = fp.xl, yl = fp.yl, xh = fp.xh, yh = fp.yh;
        float weight = fp.weight;
        int col_lo = std::max(0, (int)(xl / bin_w));
        int col_hi = std::min(nx - 1, (int)(xh / bin_w));
        int row_lo = std::max(0, (int)(yl / bin_h));
        int row_hi = std::min(ny - 1, (int)(yh / bin_h));
        for (int c = col_lo; c <= col_hi; c++) {
            float ox = std::min(xh, (c + 1) * bin_w) - std::max(xl, c * bin_w);
            if (ox <= 0.0f) continue;
            for (int r = row_lo; r <= row_hi; r++) {
                float oy = std::min(yh, (r + 1) * bin_h) - std::max(yl, r * bin_h);
                if (oy <= 0.0f) continue;
                if (g_deterministic) {           // shared bin: same reduction as computeOverlaps
                    density[c * ny + r] += ox * oy * weight;
                } else {
                    #pragma omp atomic
                    density[c * ny + r] += ox * oy * weight;
                }
            }
        }
    };

    // Unlike computeOverlaps this scatter has no per-node list to replay, and it is a metric
    // rather than the solver's field, so the deterministic path simply stays serial. It is a
    // few percent of the iteration and the whole function is memory-bound either way.
    auto deposit_pass = [&](const auto& node_vec, bool clamp_node, bool skip_macros = false) {
        #pragma omp parallel for schedule(dynamic, 512) if(!g_deterministic)
        for (int i = 0; i < (int)node_vec.size(); i++) {
            if (skip_macros && node_vec[i]->isMovableMacro()) continue;
            deposit(node_vec[i], clamp_node);
        }
    };

    // Fixed baseline at exact size, capped per bin (mirrors clampFixedDensity). The passes are
    // ordered (the cap reads the fixed baseline); within a pass the nodes are independent.
    deposit_pass(db.getFixedComponents(), false);
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < (int)density.size(); i++) density[i] = std::min(density[i], cap);

    // Movable real cells (clamped when requested). Fillers included only for the diagnostic
    // that mirrors XPlace's filler-inclusive GP stop signal (default: excluded). exclude_macros
    // mirrors XPlace's zero_macro_grad (evaluator.py::get_obj_overflow) -- movable macros dropped
    // from the deposit entirely, denominator (getTotalMovableArea()) unchanged.
    deposit_pass(db.getMovableComponents(), smooth, exclude_macros);
    if (include_fillers) deposit_pass(db.getFillers(), smooth);

    // A linear scan of a contiguous array: already memory-bound, so it stays serial rather than
    // buying a fraction of a millisecond at the cost of an ordering caveat.
    float overflow_area = 0.0f;
    for (float d : density) overflow_area += std::max(0.0f, d - cap);

    if (out_density) *out_density = density; // area deposited per bin, index = col*ny + row

    return overflow_area / (db.getTotalMovableArea() + 1e-8f);
}

/**
 * @brief Dump the real-cell bin-density map ρ for offline comparison with XPlace.
 *
 * Writes two CSVs — smoothed (clamped footprints, the smoothed field the optimizer
 * minimizes) and exact (sharp footprints, the physical density) — at the current
 * (restored best) placement, using the same deposit as computeOverflow (fillers
 * excluded, fixed baseline capped). ρ = deposited_area / bin_area, so ρ = 1 means a
 * bin exactly at target_density-normalized capacity. Layout: one text row per grid
 * row y (0 = bottom), comma-separated over columns x (0 = left).
 */
void Placer::dumpBinDensity(const std::string& path_prefix)
{
    const int nx = grid.getBinsPerRow();
    const int ny = grid.getBinsPerCol();
    const float bin_area = grid.getBinWidth() * grid.getBinHeight();

    for (bool smooth : {true, false}) {
        std::vector<float> density;
        computeOverflow(smooth, &density); // fills density[col*ny + row] (area per bin)

        std::string fname = path_prefix + (smooth ? "_rho_smoothed.csv" : "_rho_exact.csv");
        std::ofstream out(fname);
        for (int r = 0; r < ny; r++) {
            for (int c = 0; c < nx; c++)
                out << (c ? "," : "") << (density[c * ny + r] / bin_area);
            out << "\n";
        }
        Logger::log_info("Dumped " + std::string(smooth ? "smoothed" : "exact") +
                         " bin-density map (" + std::to_string(nx) + "x" +
                         std::to_string(ny) + ") -> " + fname);
    }
}

Gradient Placer::computeElectrostaticForce(Node* node_p)
{
    Gradient electro_force;

    // for each bin that this node overlaps, accumulate the field weighted by the
    // node's OVERLAP AREA with that bin (bo.overlap). The area weighting is required:
    // the force is q*E distributed over the covered bins by overlap, i.e.
    // sum_bins overlap_area * eField -- confirmed vs DREAMPlace electric_force_cuda_kernel
    // (area * field_map) and Xplace density_map_cuda_backward (overlap_area * grad_mat).
    for (BinOverlap bo : node_p->getBinOverlaps()) {
        float coeff = density_weight * bo.bin_p->local_density_weight * bo.overlap;
        electro_force += coeff * bo.bin_p->eField;
    }

    return electro_force;
}

AIEPLACE_NAMESPACE_END
