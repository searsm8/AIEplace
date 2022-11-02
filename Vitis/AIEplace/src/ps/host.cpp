/*
* Host application for AIEplace
* transfers data to PL mm2s data mover.
* Receives result from PL s2mm.
*/

#include "AIEplaceGraph.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fstream>
#include "data.h"

// This is used for the PL Kernels
#include "xrt.h"
#include "experimental/xrt_kernel.h"

// Using the ADF API that call XRT API
#include "adf/adf_api/XRTConfig.h"


static std::vector<char>
load_xclbin(xrtDeviceHandle device, const std::string& fnm)
{
	if (fnm.empty())
		throw std::runtime_error("No xclbin specified");

	// load bit stream
	std::ifstream stream(fnm);
	stream.seekg(0,stream.end);
	size_t size = stream.tellg();
	stream.seekg(0,stream.beg);

	std::vector<char> header(size);
	stream.read(header.data(),size);

	auto top = reinterpret_cast<const axlf*>(header.data());
	if (xrtDeviceLoadXclbin(device, top))
		throw std::runtime_error("Xclbin loading failed");

	return header;
}

#define FFT_SIZE 32
#define COLS FFT_SIZE
#define ROWS FFT_SIZE
#define BO_SIZE ROWS*COLS*2

int main(int argc, char ** argv)
{
	//////////////////////////////////////////
	// Open xclbin
	//////////////////////////////////////////
	auto dhdl = xrtDeviceOpen(0); // Open Device the local device
	if(dhdl == nullptr)
		throw std::runtime_error("No valid device handle found. Make sure using right xclOpen index.");
    auto xclbin = load_xclbin(dhdl, "a.xclbin");
    auto top = reinterpret_cast<const axlf*>(xclbin.data());
    adf::registerXRT(dhdl, top->m_header.uuid);

	
	//////////////////////////////////////////
	// input memory
	// Allocating the input size of COLS to MM2S
	// This is using low-level XRT call xclAllocBO to allocate the memory
	//////////////////////////////////////////	
    
	xrtBufferHandle in_bohdl = xrtBOAlloc(dhdl, BO_SIZE*sizeof(float), 0, 0);
	auto in_bomapped = reinterpret_cast<uint32_t*>(xrtBOMap(in_bohdl));
	memcpy(in_bomapped, cfloat_input, BO_SIZE*sizeof(float));
	printf("Input memory virtual addr 0x%px\n", in_bomapped);

	#if defined(__SYNCBO_ENABLE__) 
		xrtBOSync(in_bohdl, XCL_BO_SYNC_BO_TO_DEVICE, COLS * sizeof(int16_t) * 2 , 0);
	#endif
	
	//////////////////////////////////////////
	// transpose buffers
	xrtBufferHandle transpose_bohdl = xrtBOAlloc(dhdl, BO_SIZE*sizeof(float), 0, 0);
	float* transpose_bomapped = reinterpret_cast<float*>(xrtBOMap(transpose_bohdl));
	memset(transpose_bomapped, 0xABCDEF00, BO_SIZE*sizeof(float));
	printf("Output memory virtual addr 0x%px\n", transpose_bomapped);

	//xrtBufferHandle transpose_out_bohdl = xrtBOAlloc(dhdl, BO_SIZE*sizeof(float), 0, 0);
	//float* transpose_out_bomapped = reinterpret_cast<float*>(xrtBOMap(transpose_out_bohdl));
	//memset(transpose_out_bomapped, 0xABCDEF00, BO_SIZE*sizeof(float));
	//printf("Output memory virtual addr 0x%px\n", transpose_out_bomapped);

	//////////////////////////////////////////
	// output memory
	// Allocating the output size of COLS to S2MM
	// This is using low-level XRT call xclAllocBO to allocate the memory
	//////////////////////////////////////////
	xrtBufferHandle out_bohdl = xrtBOAlloc(dhdl, BO_SIZE*sizeof(float), 0, 0);
	float* out_bomapped = reinterpret_cast<float*>(xrtBOMap(out_bohdl));
	memset(out_bomapped, 0xABCDEF00, BO_SIZE*sizeof(float));
	printf("Output memory virtual addr 0x%px\n", out_bomapped);
	
	//////////////////////////////////////////
	// mm2s ip
	// Using the xrtPLKernelOpen function to manually control the PL Kernel
	// that is outside of the AI Engine graph
	//////////////////////////////////////////
	
	xrtKernelHandle mm2s_khdl = xrtPLKernelOpen(dhdl, top->m_header.uuid, "mm2s:{mm2s_1}");
	// Need to provide the kernel handle, and the argument order of the kernel arguments
	// Here the in_bohdl is the input buffer, the nullptr is the streaming interface and must be null,
	// lastly, the size of the data. This info can be found in the kernel definition. 
	xrtRunHandle mm2s_rhdl = xrtKernelRun(mm2s_khdl, in_bohdl, nullptr, BO_SIZE);
	printf("AIEplace run mm2s_1 (size: %d bytes)\n", BO_SIZE);

	//transpose ip
	xrtKernelHandle transpose_khdl = xrtPLKernelOpen(dhdl, top->m_header.uuid, "transpose:{transpose_1}");
	xrtRunHandle transpose_rhdl = xrtKernelRun(transpose_khdl, transpose_bohdl, nullptr, nullptr, COLS, ROWS);
	printf("AIEplace run transpose_1 (size: %d bytes)\n", BO_SIZE);
	
	//////////////////////////////////////////
	// s2mm ip
	// Using the xrtPLKernelOpen function to manually control the PL Kernel
	// that is outside of the AI Engine graph
	//////////////////////////////////////////
	xrtKernelHandle s2mm_khdl = xrtPLKernelOpen(dhdl, top->m_header.uuid, "s2mm_tm:{s2mm_tm_1}");
	// Need to provide the kernel handle, and the argument order of the kernel arguments
	// Here the out_bohdl is the output buffer, the nullptr is the streaming interface and must be null,
	// lastly, the size of the data. This info can be found in the kernel definition. 
	xrtRunHandle s2mm_rhdl = xrtKernelRun(s2mm_khdl, out_bohdl, nullptr, COLS, ROWS);
	printf("AIEplace run s2mm_%d (size: %d floats)\n", 1, BO_SIZE);

	//////////////////////////////////////////
	// graph execution for AIE
	//////////////////////////////////////////	
    AIEplaceGraph graph;
	
	printf("AIEplace graph init. This does nothing because CDO in boot PDI already configures AIE.\n");
	graph.init();
	
	printf("AIEplace graph running for %d iterations...\n", ROWS);
	graph.run(ROWS);
	
	graph.end();
	printf("AIEplace graph end!\n");

	//////////////////////////////////////////
	// wait for mm2s done
	//////////////////////////////////////////	
	auto state = xrtRunWait(mm2s_rhdl);
	std::cout << "mm2s completed with status(" << state << ")\n";
	xrtRunClose(mm2s_rhdl);
	xrtKernelClose(mm2s_khdl);
	
	//////////////////////////////////////////
	// wait for transpose done
	//////////////////////////////////////////	
	
	state = xrtRunWait(transpose_rhdl);
	std::cout << "transpose completed with status(" << state << ")\n";
	xrtRunClose(transpose_rhdl);
	xrtKernelClose(transpose_khdl);

	//////////////////////////////////////////
	// wait for s2mm done
	//////////////////////////////////////////	
	state = xrtRunWait(s2mm_rhdl);
	std::cout << "s2mm completed with status(" << state << ")\n";
	xrtRunClose(s2mm_rhdl);
	xrtKernelClose(s2mm_khdl);

	#if defined(__SYNCBO_ENABLE__) 
		xrtBOSync(out_bohdl, XCL_BO_SYNC_BO_FROM_DEVICE, COLS * sizeof(int) , 0);
	#endif
	
	//////////////////////////////////////////
	// Comparing the execution data to the golden data
	//////////////////////////////////////////	
	
	int errorCount = 0;
	int successCount = 0;
	printf("\n\n###DCT CHECK###\n");
	for (int i = 0; i < COLS*2*ROWS; i++) {
			if (abs(out_bomapped[i] - golden_2d_dct[i]) > abs(golden_dct[i]/100)) {
				printf("Error found @ %d, %.2f != %.2f\n", i, out_bomapped[i], golden_2d_dct[i]); errorCount++; }
			else { successCount++; printf("Correct output @ %d, %.2f == %.2f\n", i, out_bomapped[i], golden_2d_dct[i]); }
	}

	//printf("\n\n###IDCT CHECK###\n");
	//for (int i = 0; i < COLS*2*ROWS; i++) {
	//		if (abs(out_bomapped[1][i] - golden_idct[i]) > abs(golden_idct[i]/100)) {
	//			printf("Error found @ %d, %.2f != %.2f\n", i, out_bomapped[1][i], golden_idct[i]); errorCount++; }
	//		else { successCount++; printf("Correct output @ %d, %.2f == %.2f\n", i, out_bomapped[1][i], golden_idct[i]); }
	//}

	//printf("\n\n###IDXST CHECK###\n");
	//for (int i = 0; i < COLS*2*ROWS; i++) {
	//		if (abs(out_bomapped[2][i] - golden_idxst[i]) > abs(golden_idxst[i]/100)) {
	//			printf("Error found @ %d, %.2f != %.2f\n", i, out_bomapped[2][i], golden_idxst[i]); errorCount++; }
	//		else { successCount++; printf("Correct output @ %d, %.2f == %.2f\n", i, out_bomapped[2][i], golden_idxst[i]); }
	//}

	printf("%d correct results and %d errors\n", successCount, errorCount);
	if (errorCount)
		printf("DCT TEST FAILED\n");
	else
		printf("DCT TEST PASSED\n");

	//TODO: Print runtime??
	
	//////////////////////////////////////////
	// clean up XRT
	//////////////////////////////////////////	
    
	std::cout << "Releasing remaining XRT objects...\n";
	//xrtBOUnmap(dhdl, in_bohdl, in_bomapped);
	//xrtBOUnmap(dhdl, out_bohdl, out_bomapped);
	xrtBOFree(in_bohdl);
	xrtBOFree(transpose_bohdl);
	xrtBOFree(out_bohdl);
	xrtDeviceClose(dhdl);
	
	return errorCount;
}
