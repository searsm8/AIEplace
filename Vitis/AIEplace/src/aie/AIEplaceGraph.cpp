
#include "AIEplaceGraph.h"
#include "data.h"
#include <fstream>

#if !defined(__AIESIM__) && !defined(__X86SIM__) && !defined(__ADF_FRONTEND__)
    #include "adf/adf_api/XRTConfig.h"
    #include "experimental/xrt_kernel.h"
#endif

#define FFT_SIZE 16
#define COLS FFT_SIZE
#define ROWS FFT_SIZE
#define BUFF_SIZE ROWS*COLS*2

using namespace adf;
AIEplaceGraph gr; // graph declaration MUST be outside of main function

int main(void) {

    // Create XRT device handle for ADF API
	/////////////////////////////////////////////
#if !defined(__AIESIM__) && !defined(__X86SIM__) && !defined(__ADF_FRONTEND__)
    std::string xclbinFilename = "a.xclbin"; //argv[1];
    auto dhdl = xrtDeviceOpen(0);//device index=0
    xrtDeviceLoadXclbinFile(dhdl,xclbinFilename.c_str());
    xuid_t uuid;
    xrtDeviceGetXclbinUUID(dhdl, uuid);
    adf::registerXRT(dhdl, uuid);
#endif

	// Allocate global memory
	/////////////////////////////////////////////
    float* dct_inArray =(float*)GMIO::malloc(4*BUFF_SIZE);
	for(int i = 0; i < BUFF_SIZE; i++)
		dct_inArray[i] = cfloat_input[i];
    float* dct_outArray=(float*)GMIO::malloc(4*BUFF_SIZE);

	// Run the graph
	/////////////////////////////////////////////
	gr.init();
 	gr.in.gm2aie_nb(dct_inArray, 4*BUFF_SIZE);
	gr.run(NUM_ITER);
 	gr.dct_out.aie2gm_nb(dct_outArray, 4*BUFF_SIZE);
 	gr.dct_out.wait();
	gr.end();

	// Check results
	/////////////////////////////////////////////
	int error_count = 0;
	for(int i = 0; i < BUFF_SIZE; i++) {
		// if result is within 10%, it is considered correct
		if(std::abs(dct_outArray[i]) - std::abs(golden_dct[i]) > std::abs(golden_dct[i]/10) ) {
			printf("%d: %.2f != %.2f\n", i, dct_outArray[i], golden_dct[i]);
			error_count++; 
		}
	}
	if(error_count)
		printf("TEST FAIL: %d errors found.\n", error_count);
	else
		printf("TEST PASS!\n");

#if !defined(__AIESIM__) && !defined(__X86SIM__) && !defined(__ADF_FRONTEND__)
    xrtDeviceClose(dhdl);
#endif
 	return error_count;
}
