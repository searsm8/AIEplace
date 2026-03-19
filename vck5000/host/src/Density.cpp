// Density.cpp
// Electric field and density computation functions
// Separated from AIEplace.cpp for better organization

#include "AIEplace.h"
#include "DCT.h"
#include <cmath>

AIEPLACE_NAMESPACE_BEGIN

void Placer::computeElectricFields()
{
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

// TODO: function not used? Marked for deletion
void Placer::normalizeElectricFields()
{
    float max_abs = 0;
    // find the max absolute value of all eFields
    for (int x = 0; x < grid.getBinsPerRow(); x++) {
        for (int y = 0; y < grid.getBinsPerCol(); y++) {
            if(abs(grid.getBin(x, y).eField.x) > max_abs)
                max_abs = abs(grid.getBin(x, y).eField.x);
            if(abs(grid.getBin(x, y).eField.y) > max_abs)
                max_abs = abs(grid.getBin(x, y).eField.y);
        }
    }

    // normalize values
    for (int x = 0; x < grid.getBinsPerRow(); x++) {
        for (int y = 0; y < grid.getBinsPerCol(); y++) {
            grid.getBin(x, y).eField.x = grid.getBin(x, y).eField.x / max_abs;
            grid.getBin(x, y).eField.y = grid.getBin(x, y).eField.y / max_abs;
        }
    }

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

    // Perform 1-D DCT on rows of density (rho) matrix
    for (int row_index = 0; row_index < grid.getBinsPerCol(); row_index++)
        temp.push_back(DCT_naive(density[row_index]));

    temp = transpose(temp);

    // Perform 1-D DCT on transposed matrix
    for (int col_index = 0; col_index < grid.getBinsPerRow(); col_index++)
        a_uv.push_back(DCT_naive(temp[col_index]));

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

    // compute IDCT on all rows of Ex, and IDXST on all rows of Ey
    for (int row_index = 0; row_index < num_rows; row_index++) {
        Ex[row_index] = IDCT_naive (Ex[row_index]);
        Ey[row_index] = IDXST_naive(Ey[row_index]);
    }

    Ex = transpose(Ex);
    Ey = transpose(Ey);

    // compute IDCT on all rows of Ey, and IDXST on all rows of Ex
    for (int row_index = 0; row_index < num_rows; row_index++) {
        Ex[row_index] = IDXST_naive (Ex[row_index]);
        Ey[row_index] = IDCT_naive(Ey[row_index]);
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
    Logger::log_trace("Begin computeOverlaps()");

    for (auto item : db.getComponents())
        grid.computeBinOverlaps(item.second);

    for (auto filler : db.getFillers())
        grid.computeBinOverlaps(filler);

    // DEBUGGING
    //double total_node_area = 0;
    //for (auto item : db.getComponents())
    //    total_node_area += item.second->getArea();
    //double total_overlap = 0;
    //for (int col = 0; col < grid.getBinsPerRow(); col++) {
    //    for (int row = 0; row < grid.getBinsPerCol(); row++) {
    //        total_overlap += grid.getBin(col, row).getOverlap();
    //    }
    //}

    //Table t;
    //t.add_row(RowStream{} << "total_node_area" << total_node_area<< ""<<"");
    //t.add_row(RowStream{} << "total_overlap" << total_overlap);
    //t.add_row(RowStream{} << "single bin area" << grid.getBin(0,0).bb.getArea() << grid.getBin(7,8).bb.getArea() );
    //Logger::log("overlap", t);
}

Gradient Placer::computeElectrostaticForce(Node* node_p)
{
    Gradient electro_force;

    // for each bin that this node overlaps,
    // compute electric force based on bin overlaps
    for (BinOverlap bo : node_p->getBinOverlaps()) {
        //Bin* bin = bo.bin;
        float coeff = density_weight * bo.bin->local_density_weight;
        electro_force += coeff * bo.bin->eField;
        //electro_force.y += coeff * bin->eField.y;
    }

    return electro_force;
}

AIEPLACE_NAMESPACE_END
