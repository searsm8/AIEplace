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
#include "hpwlGraphTop.h"
#include <unistd.h>
#include <fstream>
#include <chrono>
#include <algorithm>
#if !defined(__AIESIM__) && !defined(__X86SIM__) && !defined(__ADF_FRONTEND__)
    #include "adf/adf_api/XRTConfig.h"
    #include "experimental/xrt_kernel.h"
#endif


using namespace adf;
#define BLOCK_SIZE 2*POINT_SIZE*POINT_SIZE
#define BLOCK_SIZE_in_Bytes 4*BLOCK_SIZE

const int NUM_NETS = 8*10*1;
#define NUM_COORDS NUM_NETS*NETSIZE
#define NUM_COORDS_in_Bytes 4*NUM_COORDS

class GraphDriver
{
private:

public:

    // Constructor
    GraphDriver()
    {
    }
    
    // Destructor
    ~GraphDriver()
    {
    }

    // Perform transpose on a matrix of size POINT_SIZE*POINT_SIZE
    // Each element of the matrix is complex, and so the imaginary part 
    // must follow the real part.
    void transpose(float * input, float * output, int num_cols=POINT_SIZE, int num_rows=POINT_SIZE) {
        for(int col=0; col < num_cols; col++){
            for(int row=0; row < num_rows; row++){
                output[2*(col + row*POINT_SIZE)]   = input[2*(row + col*POINT_SIZE)];   // Real part
                output[2*(col + row*POINT_SIZE)+1] = input[2*(row + col*POINT_SIZE)+1]; // Imaginary part
            }
        }
    }

    /* @brief Drive the graph to compute HPWL and partial derivatives
     *
     * @param gr: hpwl graph which will run
     * 
     * @param input: data to be fed into graph, must be of size NUM_COORDS_in_Bytes
     * 
     * @param hpwl: allocated space to store HPWL results
     * 
     * @param partials: allocated space to store partials results
     */
    void run_hpwl_graph(hpwlGraphTop * gr, float * input, float * hpwl, float * partials) {
        //std::cout<<"Running HPWL graph..." << std::endl; 
        gr->in_data.gm2aie_nb(input, NUM_COORDS_in_Bytes);

        //auto time_start = std::chrono::steady_clock::now();
        gr->run(NUM_NETS/8);
        //auto time_stop = std::chrono::steady_clock::now();
        //std::chrono::duration<double> elapsed_seconds = time_stop - time_start;
        //std::cout << "HPWL graph runtime: " << std::to_string(elapsed_seconds.count()) <<  " seconds"<<std::endl;

        //std::cout<<"aie2gm HPWL..." << std::endl; 
        gr->out_HPWL.aie2gm_nb(hpwl, NUM_NETS*4); // This can stall if not enough data!
        //std::cout<<"aie2gm Partials..." << std::endl; 
        gr->out_partials.aie2gm_nb(partials, NUM_COORDS_in_Bytes);
        gr->wait();
        gr->out_HPWL.wait();
        gr->out_partials.wait();
    }

    /* @brief Drive the graph to compute 1D-DCT
     *
     * @param gr: DCT graph which will run
     * 
     * @param input: data to be fed into graph, must be of size BLOCK_SIZE_in_Bytes
     * 
     * @param output: allocated space to store results
     */
    void run_dct_graph(gmio_graph * gr, float * input, float * output) {
        //std::cout<<"Running graph \tPOINT_SIZE = " << std::to_string(POINT_SIZE) <<std::endl;
        gr->gmio_in.gm2aie_nb(input, BLOCK_SIZE_in_Bytes);

        //auto time_start = std::chrono::steady_clock::now();
        gr->run(POINT_SIZE);
        //auto time_stop = std::chrono::steady_clock::now();
        //std::chrono::duration<double> elapsed_seconds = time_stop - time_start;
        //std::cout << "DCT graph runtime: " << std::to_string(elapsed_seconds.count()) <<  " seconds"<<std::endl;

        gr->gmio_out.aie2gm_nb(output, BLOCK_SIZE_in_Bytes);
        gr->gmio_out.wait();
        gr->wait();
    }

    void run_dct_2d(gmio_graph * gr, float * input, float * temp) {
        // Run DCT graph on rows
        run_dct_graph(gr, input, temp);

        // Transpose, then run DCT graph on cols
        transpose(temp, input);
        run_dct_graph(gr, input, temp);

        // Transpose back
        transpose(temp, input);

        // 2D DCT result is now in input array
    }

    int checkHPWLOutput(std::string name, int size, float * output, float * golden) {
        const int MAX_PRINTS = 5;
        const float err_tolerance = 0.01; //results are considered correct if within 1%
        int print_count = 0, failure_count = 0, max_error_ind = 0;
        float error, total_error = 0, max_error = 0;
        for(unsigned int i = 0; i < size; i++){
            error = std::abs(output[i] / golden[i] - 1);

            if(error > max_error && std::abs(golden[i]) > 1) {
                max_error = error;
                max_error_ind = i;
            }
            total_error += error;
            if(error > err_tolerance){
                if(print_count++ < MAX_PRINTS)
                    std::cout<<name<<" ERROR: output["<<i<<"]="<<output[i]<<",\tgolden="<<golden[i]<<"\terror="<<error*100<<"%"<<std::endl;
                failure_count++;
            }
        }
        float avg_error = total_error / float(size);
        std::cout<<"***"<< failure_count <<" errors were detected.***"<<std::endl;
        std::cout<<"Max error: " << max_error*100 << "%" << "\toutput["<<max_error_ind<<"]="<<output[max_error_ind]<<",\tgolden="<<golden[max_error_ind]<<std::endl;
        std::cout<<"Average error: " << avg_error*100 << "%" << std::endl << std::endl;
        return failure_count;
    }

    int checkDCTOutput(std::string name, float * output, float * golden) {
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
                    std::cout<<name<<" ERROR: output["<<i<<"]="<<output[i]<<",\tgolden="<<golden[i]<<"\terror="<<error*100<<"%"<<std::endl;
                failure_count++;
            }
        }
        float avg_error = total_error / float(BLOCK_SIZE);
        std::cout<<"***"<< failure_count <<" errors were detected.***"<<std::endl;
        std::cout<<"Max error: " << max_error*100 << "%" << "\toutput["<<max_error_ind<<"]="<<output[max_error_ind]<<",\tgolden="<<golden[max_error_ind]<<std::endl;
        std::cout<<"Average error: " << avg_error*100 << "%" << std::endl << std::endl;
        return failure_count;
    }
};