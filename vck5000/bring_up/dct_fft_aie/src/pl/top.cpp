// top.cpp -- minimal PL kernel for the DCT-transpose <-> AIE-FFT integration test.
//
// This is a STRIPPED copy of the AIEplace pl_algo top: it exposes ONLY dct_transpose_pass
// and the pieces it needs -- two DDR bundles (mat_in/mat_out) and the 16 AXIS streams that
// drive the 8-lane AIE forward-FFT pool. The stream ports, their `#pragma HLS INTERFACE axis`
// lines, and the dct_transpose_pass call are copied VERBATIM from the sw_emu-verified pl_algo
// top.cpp, so the PL<->AIE seam under test on real silicon is byte-identical to the placer's.
// Everything else in that kernel (HPWL, density bin, spectral, force gather, iteration update,
// metrics -- 10 more DDR bundles) is intentionally absent: a failure here is unambiguously the
// DCT-transpose / FFT integration, and the xclbin builds small.
//
// The AIE does only the forward FFT (DensityFFTGraph, 8 lanes); all DCT/IDCT/IDXST pre/post
// runs in dct_transpose_pass on the PL. `xform` selects TF_DCT / TF_IDCT / TF_IDXST.

#include "formats.hpp"
#include "host_interface.hpp"
#include "modules/dct_transpose.hpp"

using namespace plalgo;

extern "C" {
void top(
    // ---- DDR matrices (group_id 0-1): GRID x GRID real, row-major, 4 MB each ----
    const float* mat_in,
    float*       mat_out,
    // ---- transform selector: TF_DCT / TF_IDCT / TF_IDXST (formats.hpp) ----
    int          xform,
    // ---- AIE FFT pool streams: 8 lanes, SEPARATE named ports (HW-wired via link.cfg,
    //      not host args). HLS does not support an array of hls::stream at the AXIS
    //      interface, so the lanes are individual scalar streams. ----
    hls::stream<axis_t>& fft_to_aie_0, hls::stream<axis_t>& fft_to_aie_1,
    hls::stream<axis_t>& fft_to_aie_2, hls::stream<axis_t>& fft_to_aie_3,
    hls::stream<axis_t>& fft_to_aie_4, hls::stream<axis_t>& fft_to_aie_5,
    hls::stream<axis_t>& fft_to_aie_6, hls::stream<axis_t>& fft_to_aie_7,
    hls::stream<axis_t>& fft_from_aie_0, hls::stream<axis_t>& fft_from_aie_1,
    hls::stream<axis_t>& fft_from_aie_2, hls::stream<axis_t>& fft_from_aie_3,
    hls::stream<axis_t>& fft_from_aie_4, hls::stream<axis_t>& fft_from_aie_5,
    hls::stream<axis_t>& fft_from_aie_6, hls::stream<axis_t>& fft_from_aie_7)
{
    // DDR AXI4 master interfaces. Widened-burst tuning copied from pl_algo top (gmem10/11):
    // the band read/transposed write want a tile-row's worth of bursts in flight.
#pragma HLS INTERFACE m_axi port=mat_in  offset=slave bundle=gmem0 num_read_outstanding=32  max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=mat_out offset=slave bundle=gmem1 num_write_outstanding=32 max_write_burst_length=64

    // AXI4-Lite control for the scalar args + kernel start.
#pragma HLS INTERFACE s_axilite port=mat_in  bundle=control
#pragma HLS INTERFACE s_axilite port=mat_out bundle=control
#pragma HLS INTERFACE s_axilite port=xform   bundle=control

    // AXIS interfaces PL to AIE (the 8-lane FFT pool) -- verbatim from pl_algo top.
#pragma HLS INTERFACE axis port=fft_to_aie_0
#pragma HLS INTERFACE axis port=fft_to_aie_1
#pragma HLS INTERFACE axis port=fft_to_aie_2
#pragma HLS INTERFACE axis port=fft_to_aie_3
#pragma HLS INTERFACE axis port=fft_to_aie_4
#pragma HLS INTERFACE axis port=fft_to_aie_5
#pragma HLS INTERFACE axis port=fft_to_aie_6
#pragma HLS INTERFACE axis port=fft_to_aie_7
#pragma HLS INTERFACE axis port=fft_from_aie_0
#pragma HLS INTERFACE axis port=fft_from_aie_1
#pragma HLS INTERFACE axis port=fft_from_aie_2
#pragma HLS INTERFACE axis port=fft_from_aie_3
#pragma HLS INTERFACE axis port=fft_from_aie_4
#pragma HLS INTERFACE axis port=fft_from_aie_5
#pragma HLS INTERFACE axis port=fft_from_aie_6
#pragma HLS INTERFACE axis port=fft_from_aie_7
#pragma HLS INTERFACE s_axilite port=return bundle=control

    // Fused transform row-pass + transpose: transform all GRID rows via the 8-lane pool,
    // written transposed. Host pairs this with g.run(GRID / FFT_LANES). Verbatim call.
    dct_transpose_pass(mat_in, mat_out, xform,
                       fft_to_aie_0, fft_to_aie_1, fft_to_aie_2, fft_to_aie_3,
                       fft_to_aie_4, fft_to_aie_5, fft_to_aie_6, fft_to_aie_7,
                       fft_from_aie_0, fft_from_aie_1, fft_from_aie_2, fft_from_aie_3,
                       fft_from_aie_4, fft_from_aie_5, fft_from_aie_6, fft_from_aie_7);
}
} // extern "C"
