// host.cpp -- "hello world" XRT host for add1_pl.
//
//   flow: fill in[] with 0..n-1 -> run the `add1` PL kernel -> read out[]
//         back -> assert out[i] == in[i] + 1 for every element, exit 0/non-zero.
//
//   usage: ./host <xclbin> [n]     (default n = 1024)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"
#include "xrt/xrt_bo.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s <xclbin> [n]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char* xclbin_path = argv[1];
    const int n = (argc >= 3) ? std::atoi(argv[2]) : 1024;
    const size_t bytes = (size_t)n * sizeof(float);

    // ---- device / xclbin ----
    // find the VCK5000 card (default bdf c1:00.1)
    const char* bdf_env = std::getenv("VCK5000_BDF"); // env var to override the default BDF
    const std::string bdf = bdf_env ? bdf_env : "c1:00.1"; // default VCK5000 BDF
    xrt::device device;
    try {
        device = xrt::device(bdf);
    } catch (const std::exception& e) {
        std::printf("failed to open device at BDF %s: %s\n", bdf.c_str(), e.what());
        return EXIT_FAILURE;
    }

    // load the xclbin and create the kernel object
    xrt::uuid uuid = device.load_xclbin(xclbin_path);
    xrt::kernel k = xrt::kernel(device, uuid, "add1");

    // ---- buffers: in (group 0), out (group 1) ----
    xrt::bo bo_in  = xrt::bo(device, bytes, k.group_id(0));
    xrt::bo bo_out = xrt::bo(device, bytes, k.group_id(1));
    float* in_map  = bo_in.map<float*>();
    float* out_map = bo_out.map<float*>();

    for (int i = 0; i < n; i++) in_map[i] = (float)i;
    std::memset(out_map, 0, bytes);
    bo_in.sync(XCL_BO_SYNC_BO_TO_DEVICE);

    // ---- run ----
    xrt::run run = k(bo_in, bo_out, n);
    run.wait();

    bo_out.sync(XCL_BO_SYNC_BO_FROM_DEVICE);

    // ---- verify ----
    for (int i = 0; i < n; i++) {
        float expect = in_map[i] + 1.0f;
        if (out_map[i] != expect) {
            std::printf("TEST FAILED  first mismatch at [%d]: got=%.3f want=%.3f\n",
                        i, out_map[i], expect);
            return EXIT_FAILURE;
        }
    }
    std::printf("TEST PASSED  n=%d  add1 on VCK5000 OK\n", n);
    return EXIT_SUCCESS;
}
