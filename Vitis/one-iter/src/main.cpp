#include "graph_driver.h" // useful functions which send data to AIE kernels
//#include "AIEplace.h" // implements the ePlace algorithm

#include "../data/density_data.h"
#include "../data/net_data.h"

using namespace adf;

hpwlGraphTop hpwl_gr;
dct_subgraph dct_gr;
idct_subgraph idct_gr;
idxst_subgraph idxst_gr;

int main(int argc, char ** argv) {

    #if !defined(__AIESIM__) && !defined(__X86SIM__) && !defined(__ADF_FRONTEND__)
        // Create XRT device handle for ADF API
        char* xclbinFilename = argv[1];
        auto dhdl = xrtDeviceOpen(0);//device index=0
        xrtDeviceLoadXclbinFile(dhdl,xclbinFilename);
        xuid_t uuid;
        xrtDeviceGetXclbinUUID(dhdl, uuid);
        adf::registerXRT(dhdl, uuid);
    #endif

        hpwl_gr.init();
        dct_gr.init();
        idct_gr.init();
        idxst_gr.init();

    GraphDriver driver = GraphDriver();

    // Allocate global memory for 2D DCTs
    float * temp=(float*)GMIO::malloc(BLOCK_SIZE_in_Bytes);
    float * dct_data=(float*)GMIO::malloc(BLOCK_SIZE_in_Bytes);
    float * idct_data=(float*)GMIO::malloc(BLOCK_SIZE_in_Bytes);
    float * idxst_data=(float*)GMIO::malloc(BLOCK_SIZE_in_Bytes);

    // Allocate global memory for HPWL
    float * net_data = (float*)GMIO::malloc(NUM_COORDS_in_Bytes);
    float * hpwl_result = (float*)GMIO::malloc(NUM_COORDS_in_Bytes);
    float * partials_result = (float*)GMIO::malloc(NUM_COORDS_in_Bytes);
    std::cout<<"GMIO::malloc completed"<<std::endl;

    // Read input data from files using ifstream?

    auto total_time_start = std::chrono::steady_clock::now();
    std::copy(initial_net_coords, initial_net_coords + NUM_COORDS, net_data );
    driver.run_hpwl_graph(&hpwl_gr, net_data, hpwl_result, partials_result);
    //hpwl_graph_run(&hpwl_graph, net_data, hpwl_result, partials_result);
    //hpwl_graph_wait(&hpwl_graph);

    // Setup inputs, Perform 2D-DCT (BLOCKING)
    std::copy(dct_input, dct_input + BLOCK_SIZE, dct_data);
    driver.run_dct_2d(&dct_gr, dct_data, temp);

    // Setup inputs, Perform 2D-IDCT (BLOCKING)
    std::copy(dct_data, dct_data + BLOCK_SIZE, idct_data);
    driver.run_dct_2d(&idct_gr, idct_data, temp);

    // Setup inputs, Perform 2D-IDXST (BLOCKING)
    std::copy(dct_data, dct_data + BLOCK_SIZE, idxst_data);
    driver.run_dct_2d(&idxst_gr, idxst_data, temp);

    
    auto total_time_stop = std::chrono::steady_clock::now();
    std::chrono::duration<double> total_elapsed_seconds= total_time_stop - total_time_start;
    std::cout << "Total Elapsed Time: " << std::to_string(total_elapsed_seconds.count()) <<  " seconds"<<std::endl;

    std::cout<<"GMIO transactions finished"<<std::endl;
    int hpwl_error_count      = driver.checkHPWLOutput("HPWL", NUM_NETS, hpwl_result, golden_hpwl);
    int partials_error_count  = driver.checkHPWLOutput("PARTIALS", NUM_COORDS, partials_result, golden_partials);

    int dct_error_count  = driver.checkDCTOutput("DCT", dct_data, golden_dct_2d);
    int idct_error_count = driver.checkDCTOutput("IDCT", idct_data, golden_idct_2d);
    int idxst_error_count= driver.checkDCTOutput("IDXST", idxst_data, golden_idxst_2d);

    // Clean up
    GMIO::free(temp);
    GMIO::free(dct_data);
    GMIO::free(idct_data);
    GMIO::free(idxst_data);
 

    #if !defined(__AIESIM__) && !defined(__X86SIM__) && !defined(__ADF_FRONTEND__)
        xrtDeviceClose(dhdl);
    #endif

        hpwl_gr.end();
        dct_gr.end();
        idct_gr.end();
        idxst_gr.end();

    int total_error_count = dct_error_count + idct_error_count + idxst_error_count;
    return 0;
    return total_error_count;
};