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
    TIME_FUNCTION();
    computeOverlaps();          // update the density ρ at probe positions

    if(density_method == "aie") {
        #ifdef USE_XILINX_XRT
            computeElectricFields_AIE(); // Accelerated compute on AIEs
        #else
            Logger::log_error("density_method 'aie' requires XRT. Recompile with BUILD_XRT=1 or use 'cpu'");
            exit(1);
        #endif
    } else if(density_method == "cpu") {
        computeElectricFields_DCT(); // Compute E-fields on CPU using DCT for verification
        //computeElectricFields_CPU(); // Compute E-fields using naive algorithm 
    } else { 
        Logger::log_error("Invalid density_compute_method specified in config file"); 
        exit(1);
    }
}

/***************
 * XRT/AIE ACCELERATION FUNCTIONS - VCK5000 only
 ****************/

#ifdef USE_XILINX_XRT

/*
 * @brief On AIEs, compute Electric fields using 2D-DCT method
 *
**/
void Placer::computeElectricFields_AIE()
{
    Logger::log_trace("Begin computeElectricFields_AIE()");

    // Call AIE graph_driver to accelerate computation
    std::vector< std::vector<float> > density = grid.getBinDensities(); // rho
    std::vector< std::vector<float> > temp;

    //DEBUGGING: print out the density (rho) matrix
    //for( int x_index = 0; x_index < BINS_PER_ROW; x_index++)
    //{
    //    for( int y_index = 0; y_index < BINS_PER_ROW; y_index++)
    //    {
    //        cout << density[x_index][y_index] << " ";
    //    }
    //    cout << endl;
    //}
    //cout << endl;

    float * input_data  = new float[2*BINS_PER_ROW];
    float * output_data = new float[2*BINS_PER_ROW];

    // Send the density (rho) matrix into the AIE, one row at a time, for 1D-DCTs

    for(int row = 0; row < BINS_PER_ROW; row++) {
        for(int col = 0; col < BINS_PER_ROW; col++) {
        input_data[2*col] = density[row][col]; // real part
        input_data[2*col+1] = 0; // imaginary part
        }

    density_driver[0].send_packet(input_data);
    density_driver[0].receive_packet(output_data);

    std::vector<float> res;
    for(int col = 0; col < BINS_PER_ROW; col++)
        res.push_back(output_data[2*col]);
    temp.push_back(res);

    //std::vector<float> test_output = DCT_naive(test_data);
    //for(int i = 0; i < BINS_PER_ROW; i++) {
    //    cout << test_output[i] << " ";
    //} cout << endl;

    }

    // Send the density (rho) matrix into the AIE, one column at a time, to complete 2D-DCT
    //cout << "Input" << std::setprecision(2) << endl;
    for(int col = 0; col < BINS_PER_ROW; col++) {
        for(int row = 0; row < BINS_PER_ROW; row++) { // looping order performs DCT on columns
        input_data[2*row] = temp[row][col];
        input_data[2*row+1] = 0;
        }

        // Send data to DCT graph
        density_driver[0].send_packet(input_data);
        density_driver[0].receive_packet(output_data);

        //cout << endl << "AIE DCT output:" << endl << std::setprecision(2);
        // Store the result a_uv transposed (for comparison)
        for(int row = 0; row < BINS_PER_ROW; row++) {
            grid.getBin(row, col).a_uv = output_data[2*row];
            //cout << output_data[2*row] << " ";
        }
        //cout << endl;

    }

    // Compute Ex
    temp.clear();
    // Setup input for IDCT
    double w_u, w_v, denom_inv;
    for(int row = 0; row < BINS_PER_ROW; row++) { //looping params implement transpose!
        //cout << endl << "IDCT input to AIE:" << endl << std::setprecision(2);
        for(int col = 0; col < BINS_PER_ROW; col++) {
            if(row == 0 && col == 0)
                { w_u = 0; w_v = 0; denom_inv = 0;} // for 0, 0 we avoid division by 0
            else {
                w_u = 2*M_PI*row / BINS_PER_ROW;
                w_v = 2*M_PI*col / BINS_PER_ROW;
                denom_inv = 1 / (w_u*w_u + w_v*w_v);
            }
            input_data[2*col] = grid.getBin(row, col).a_uv * w_u * denom_inv;
            input_data[2*col+1] = 0; // imaginary part is expected for FFT input
            //cout << input_data[2*col] << " ";
        }
        //cout << endl;


        // Send data to IDCT graph
        density_driver[1].send_packet(input_data);
        density_driver[1].receive_packet(output_data);

        //cout << endl << "IDCT output from AIE:" << endl << std::setprecision(2);
        std::vector<float> res;
        for(int col = 0; col < BINS_PER_ROW; col++) {
            res.push_back(output_data[2*col]);
            //cout << output_data[2*col] << " ";
        }
        //cout << endl;
        temp.push_back(res);
    }

    for(int col = 0; col < BINS_PER_ROW; col++) {
        for(int row = 0; row < BINS_PER_ROW; row++) { // looping order performs IDXST on columns
            input_data[2*row] = temp[row][col];
            input_data[2*row+1] = 0;
        }

        // Send data to IDXST graph
        density_driver[2].send_packet(input_data);
        density_driver[2].receive_packet(output_data);

        //cout << endl << "IDXST output:" << endl << std::setprecision(2);
        // Store the result Ex transposed (for comparison)
        for(int row = 0; row < BINS_PER_ROW; row++) {
            grid.getBin(row, col).eField.x = output_data[2*row];
            //cout << output_data[2*row] << " ";
        }
        //cout << endl;
    }

    // Compute Ey
    temp.clear();
    // Setup input for IDXST
    for(int row = 0; row < BINS_PER_ROW; row++) { //looping params implement transpose!
        //cout << endl << "IDXST input to AIE:" << endl << std::setprecision(2);
        for(int col = 0; col < BINS_PER_ROW; col++) {
            if(row == 0 && col == 0)
                { w_u = 0; w_v = 0; denom_inv = 0;} // for a(0, 0) we avoid division by 0 (remove dc component)
            else {
                w_u = 2*M_PI*row / BINS_PER_ROW;
                w_v = 2*M_PI*col / BINS_PER_ROW;
                denom_inv = 1 / (w_u*w_u + w_v*w_v);
            }
            input_data[2*col] = grid.getBin(row, col).a_uv * w_v * denom_inv;
            input_data[2*col+1] = 0;
            //cout << input_data[2*col] << " ";
        }
        //cout << endl;


        // Send data to IDXST graph
        density_driver[2].send_packet(input_data);
        density_driver[2].receive_packet(output_data);

        //cout << endl << "IDXST output from AIE:" << endl << std::setprecision(2);
        std::vector<float> res;
        for(int col = 0; col < BINS_PER_ROW; col++) {
            res.push_back(output_data[2*col]);
            //cout << output_data[2*col] << " ";
        }
        //cout << endl;
        temp.push_back(res);
    }

    for(int col = 0; col < BINS_PER_ROW; col++) {
        for(int row = 0; row < BINS_PER_ROW; row++) { // looping order performs IDCT on columns
            input_data[2*row] = temp[row][col];
            input_data[2*row+1] = 0;
        }

        // Send data to IDCT graph
        density_driver[1].send_packet(input_data);
        density_driver[1].receive_packet(output_data);

        //cout << endl << "IDCT output:" << endl << std::setprecision(2);
        // Store the result Ex transposed (for comparison)
        for(int row = 0; row < BINS_PER_ROW; row++) {
            grid.getBin(row, col).eField.y = output_data[2*row];
            //cout << output_data[2*row] << " ";
        }
        //cout << endl;
    }
}

#endif // USE_XILINX_XRT


/***************
 * CPU FUNCTIONS - Always available
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

/*
 * @brief On CPU, compute Electric fields using 2D-DCT method
 *
**/
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

/* @brief: Compute the intermediate term a_uv using DCTs*/
void Placer::compute_a_uv_DCT()
{
    std::vector< std::vector<float> > density = grid.getBinDensities(); // rho
    std::vector< std::vector<float> > temp;
    std::vector< std::vector<float> > a_uv;

    // Perform 1-D DCT on rows of density (rho) matrix (FFT, O(N log N); verified == DCT_naive)
    for (int row_index = 0; row_index < grid.getBinsPerCol(); row_index++)
        temp.push_back(DCT_fft(density[row_index], dct_normalize));

    temp = transpose(temp);

    // Perform 1-D DCT on transposed matrix
    for (int col_index = 0; col_index < grid.getBinsPerRow(); col_index++)
        a_uv.push_back(DCT_fft(temp[col_index], dct_normalize));

    a_uv = transpose(a_uv);

    for (int u = 0; u < grid.getBinsPerRow(); u++)
        for (int v = 0; v < grid.getBinsPerCol(); v++) {
           grid.getBin(u, v).a_uv = a_uv[u][v];
        }
}

/* @brief: Compute the eField values using DCTs*/
void Placer::compute_eField_DCT()
{
    int num_rows = grid.getBinsPerCol();
    int num_cols = grid.getBinsPerRow();
    std::vector< std::vector<float> > Ex     (num_rows, std::vector<float>(num_cols));
    std::vector< std::vector<float> > Ey     (num_rows, std::vector<float>(num_cols));
    std::vector< std::vector<float> > a_uv = grid.get_a_uv();

    float w = 2 * M_PI / num_cols;
    Ex[0][0] = 0; Ey[0][0] = 0;

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

    // Inverse transforms use dct_normalize_inverse (NOT dct_normalize). The forward a_uv already
    // carries the 1/N^2; re-applying 1/N here (legacy dct_normalize_inverse=true) adds a spurious
    // 1/N^2 vs the naive DREAMPlace field, inflating lambda by ~N^2. false = field-faithful inverse.
    // compute IDCT on all rows of Ex, and IDXST on all rows of Ey (FFT; verified == naive)
    for (int row_index = 0; row_index < num_rows; row_index++) {
        Ex[row_index] = IDCT_fft (Ex[row_index], dct_normalize_inverse);
        Ey[row_index] = IDXST_fft(Ey[row_index], dct_normalize_inverse);
    }

    Ex = transpose(Ex);
    Ey = transpose(Ey);

    // compute IDCT on all rows of Ey, and IDXST on all rows of Ex
    for (int row_index = 0; row_index < num_rows; row_index++) {
        Ex[row_index] = IDXST_fft (Ex[row_index], dct_normalize_inverse);
        Ey[row_index] = IDCT_fft(Ey[row_index], dct_normalize_inverse);
    }

    Ex = transpose(Ex);
    Ey = transpose(Ey);

    // Put results in the grid bins
    for (int x = 0; x < num_cols; x++) {
        for (int y = 0; y < num_rows; y++) {
            grid.getBin(x, y).eField.x = Ex[x][y];
            grid.getBin(x, y).eField.y = Ey[x][y];
        }
    }
}


void Placer::computeOverlaps()
{
    TIME_FUNCTION();
    Logger::log_trace("Begin computeOverlaps()");

    // Pass 1: Fixed components — their density is clamped so bins fully covered
    // by fixed macros register as "at capacity" but not overflowed.
    for (auto item : db.getComponents())
        if (item.second->getStatus() == FIXED)
            grid.computeBinOverlaps(item.second);

    grid.clampFixedDensity(target_density);

    // Pass 2: Movable components and fillers — any density on top of
    // the clamped fixed baseline counts as real overflow.
    for (auto item : db.getComponents())
        if (item.second->getStatus() != FIXED)
            grid.computeBinOverlaps(item.second);

    for (auto filler : db.getFillers())
        grid.computeBinOverlaps(filler);
}

/*
 * @brief Overflow metric with fillers excluded. clamp=true gives XPlace's *masked* overflow
 * (the global-placement convergence signal); clamp=false gives the *exact* physical overflow.
 *
 * Same formula as Grid::computeTotalOverflow (sum of per-bin excess over target*bin_area,
 * normalized by movable area), evaluated on an independently-built density map so the two
 * variants can be reported side by side. When clamp=true each movable cell's footprint is
 * inflated to at least sqrt(2) bins per dimension with an area-conserving weight
 * (real_area/clamped_area) and shifted to stay in-die — matching Grid::computeBinOverlaps —
 * so sub-bin cells are smeared to grid resolution rather than spiking a single bin. Fixed
 * macros form a per-bin-capped baseline (mirrors clampFixedDensity); fillers are excluded.
 *
 * Why masked matters: it is the smoothed density the electrostatic optimizer actually
 * minimizes, so it descends cleanly to the stop threshold. The exact overflow re-measures
 * with sharp footprints, whose sub-bin quantization spikes leave it floored above threshold
 * even for a well-spread placement (the markv1 "can't reach 0.07" effect).
 */
float Placer::computeOverflow(bool clamp, std::vector<float>* out_density)
{
    const int nx = grid.getBinsPerRow();
    const int ny = grid.getBinsPerCol();
    const float bin_w = grid.getBinWidth();
    const float bin_h = grid.getBinHeight();
    const float cap   = bin_w * bin_h * target_density;   // per-bin capacity
    const float min_w = bin_w * (float)M_SQRT2;           // clamp each dim to >= sqrt(2)*bin
    const float min_h = bin_h * (float)M_SQRT2;
    const float grid_w = nx * bin_w;
    const float grid_h = ny * bin_h;

    std::vector<float> density(nx * ny, 0.0f);            // area deposited per bin

    // Deposit a node's area over its (optionally clamped) footprint, centered on the cell and
    // shifted to stay in-die (kept in sync with Grid::computeBinOverlaps).
    auto deposit = [&](Node* node, bool clamp_node) {
        float w = node->getXsize(), h = node->getYsize();
        float cw = clamp_node ? std::max(w, min_w) : w;
        float ch = clamp_node ? std::max(h, min_h) : h;
        float weight = (cw > 0.0f && ch > 0.0f) ? (w * h) / (cw * ch) : 0.0f; // conserve total area
        float xl = node->getProbeX() + 0.5f * w - 0.5f * cw;
        float yl = node->getProbeY() + 0.5f * h - 0.5f * ch;
        if (xl + cw > grid_w) xl = grid_w - cw;
        if (yl + ch > grid_h) yl = grid_h - ch;
        if (xl < 0.0f) xl = 0.0f;
        if (yl < 0.0f) yl = 0.0f;
        float xh = xl + cw, yh = yl + ch;
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
                density[c * ny + r] += ox * oy * weight;
            }
        }
    };

    // Fixed baseline at exact size, capped per bin (mirrors clampFixedDensity).
    for (auto item : db.getComponents())
        if (item.second->getStatus() == FIXED) deposit(item.second, false);
    for (float& d : density) d = std::min(d, cap);

    // Movable real cells (clamped when requested). Fillers intentionally excluded.
    for (auto item : db.getComponents())
        if (item.second->getStatus() != FIXED) deposit(item.second, clamp);

    float overflow_area = 0.0f;
    for (float d : density) overflow_area += std::max(0.0f, d - cap);

    if (out_density) *out_density = density; // area deposited per bin, index = col*ny + row

    return overflow_area / (db.getTotalMovableArea() + 1e-8f);
}

/**
 * @brief Dump the real-cell bin-density map ρ for offline comparison with XPlace.
 *
 * Writes two CSVs — masked (clamped footprints, the smoothed field the optimizer
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

    for (bool clamp : {true, false}) {
        std::vector<float> density;
        computeOverflow(clamp, &density); // fills density[col*ny + row] (area per bin)

        std::string fname = path_prefix + (clamp ? "_rho_masked.csv" : "_rho_exact.csv");
        std::ofstream out(fname);
        for (int r = 0; r < ny; r++) {
            for (int c = 0; c < nx; c++)
                out << (c ? "," : "") << (density[c * ny + r] / bin_area);
            out << "\n";
        }
        Logger::log_info("Dumped " + std::string(clamp ? "masked" : "exact") +
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
        float coeff = density_weight * bo.bin->local_density_weight * bo.overlap;
        electro_force += coeff * bo.bin->eField;
    }

    return electro_force;
}

AIEPLACE_NAMESPACE_END
