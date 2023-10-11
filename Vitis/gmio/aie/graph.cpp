/**********
© Copyright 2020 Xilinx, Inc.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or adct_greed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
**********/
#include <adf.h>
#include "dct_subgraph.h"
#include "idct_subgraph.h"
#include "idxst_subgraph.h"
#include <unistd.h>
#include <fstream>
#include <chrono>
#include <algorithm>
#if !defined(__AIESIM__) && !defined(__X86SIM__) && !defined(__ADF_FRONTEND__)
    #include "adf/adf_api/XRTConfig.h"
    #include "experimental/xrt_kernel.h"
#endif

#include "../data/data.h"

using namespace adf;
#define BLOCK_SIZE 2*POINT_SIZE*POINT_SIZE
#define BLOCK_SIZE_in_Bytes 4*BLOCK_SIZE

dct_subgraph dct_gr;
idct_subgraph idct_gr;
idxst_subgraph idxst_gr;

//wirelength_subgraph wirelen_gr;

// Perform transpose on a matrix of size POINT_SIZE*POINT_SIZE
// Each element of the matrix is complex, and so the imaginary part 
// must follow the real part.
void transpose(float * input, float * output, int num_cols=POINT_SIZE, int num_rows=POINT_SIZE) {
    for(int col=0; col < num_cols; col++){
        for(int row=0; row < num_rows; row++){
            output[2*(col + row*POINT_SIZE)]   = input[2*(row + col*POINT_SIZE)];
            output[2*(col + row*POINT_SIZE)+1] = input[2*(row + col*POINT_SIZE)+1];
        }
    }
}

void run_graph(gmio_graph * gr, float * input, float * output) {
    std::cout<<"Running graph \tPOINT_SIZE = " << std::to_string(POINT_SIZE) <<std::endl;
    gr->gmio_in.gm2aie_nb(input, BLOCK_SIZE_in_Bytes);
    gr->run(POINT_SIZE);
    gr->gmio_out.aie2gm_nb(output, BLOCK_SIZE_in_Bytes);
    gr->wait();
    gr->gmio_out.wait();
}

void run_2d_dct(gmio_graph * gr, float * input, float * temp) {
    auto time_start = std::chrono::steady_clock::now();
    // Run DCT graph on rows
    run_graph(gr, input, temp);

    // Transpose, then run DCT graph on cols
    transpose(temp, input);
    run_graph(gr, input, temp);

    // Transpose back
    transpose(temp, input);
    // 2D DCT result is now in input array
    
    auto time_stop = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed_seconds= time_stop - time_start;
    std::cout << "Elapsed time for " << std::to_string(POINT_SIZE) << "x" << std::to_string(POINT_SIZE) << " 2D DCT is " << std::to_string(elapsed_seconds.count()) <<  " seconds"<<std::endl;
}

int checkOutput(std::string name, float * output, float * golden) {
    const int MAX_PRINTS = 5;
    const float err_tolerance = 0.01; //results are considered correct if within 1%
    int print_count = 0, failure_count = 0, max_error_ind = 0;
    float error, total_error = 0, max_error = 0;
    for(unsigned int i = 0; i < BLOCK_SIZE; i+=2){ // increment +2 to skip imaginary part (all zeroes...)
        error = std::abs(output[i] / golden[i] - 1);

        if(error > max_error && std::abs(golden[i]) > 1) {
            max_error = error;
            max_error_ind = i;
        }
        total_error += error;
        if(error > err_tolerance){
            if(print_count++ < MAX_PRINTS)
                std::cout<<name<<" ERROR: output["<<i<<"]="<<output[i]<<",\tgolden="<<golden[i]<<"\terror="<<error<<std::endl;
            failure_count++;
        }
    }
    float avg_error = total_error / float(BLOCK_SIZE);
    std::cout<<"***"<< failure_count <<" errors were detected.***"<<std::endl;
    std::cout<<"Max error: " << max_error*100 << "%" << "\toutput["<<max_error_ind<<"]="<<output[max_error_ind]<<",\tgolden="<<golden[max_error_ind]<<std::endl;
    std::cout<<"Average error: " << avg_error*100 << "%" << std::endl << std::endl;
    return failure_count;
}

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

    // Setup graphs
    dct_gr.init();
    idct_gr.init();
    idxst_gr.init();

    // Allocate global memory
    float * temp=(float*)GMIO::malloc(BLOCK_SIZE_in_Bytes);
    float * dct_data=(float*)GMIO::malloc(BLOCK_SIZE_in_Bytes);
    float * idct_data=(float*)GMIO::malloc(BLOCK_SIZE_in_Bytes);
    float * idxst_data=(float*)GMIO::malloc(BLOCK_SIZE_in_Bytes);
    std::cout<<"GMIO::malloc completed"<<std::endl;

    auto time_start = std::chrono::steady_clock::now();
    // Setup inputs, Perform 2D-DCT (BLOCKING)
    std::copy(dct_input, dct_input + BLOCK_SIZE, dct_data);
    // NO PRINTS WHILE TIMING PLEASE!!!
    //std::cout<<"DCT input copy completed."<<std::endl;
    run_2d_dct(&dct_gr, dct_data, temp);

    // Setup inputs, Perform 2D-IDCT (BLOCKING)
    std::copy(dct_data, dct_data + BLOCK_SIZE, idct_data);
    //std::cout<<"IDCT input copy completed."<<std::endl;
    run_2d_dct(&idct_gr, idct_data, temp);

    // Setup inputs, Perform 2D-IDXST (BLOCKING)
    std::copy(dct_data, dct_data + BLOCK_SIZE, idxst_data);
    //std::cout<<"IDXST input copy completed."<<std::endl;
    run_2d_dct(&idxst_gr, idxst_data, temp);

    auto time_stop = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed_seconds= time_stop - time_start;
    std::cout << "Total Elapsed Time: " << std::to_string(elapsed_seconds.count()) <<  " seconds"<<std::endl;

    std::cout<<"GMIO transactions finished"<<std::endl;

    int dct_error_count  = checkOutput("DCT", dct_data, golden_dct_2d);
    int idct_error_count = checkOutput("IDCT", idct_data, golden_idct_2d);
    int idxst_error_count= checkOutput("IDXST", idxst_data, golden_idxst_2d);

    // Clean up
    GMIO::free(temp);
    GMIO::free(dct_data);
    GMIO::free(idct_data);
    GMIO::free(idxst_data);
 
    dct_gr.end();
    idct_gr.end();
    idxst_gr.end();

#if !defined(__AIESIM__) && !defined(__X86SIM__) && !defined(__ADF_FRONTEND__)
    xrtDeviceClose(dhdl);
#endif
    int total_error_count = dct_error_count + idct_error_count + idxst_error_count;
    return total_error_count;
};
