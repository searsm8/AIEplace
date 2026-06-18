// top.cpp -- pl_algo PL kernel.
//
// Milestone B scope: drive the AIE HPWL-gradient graph through the hpwl_manager
// module as a pass-through. The host uploads an AIE input packet to DDR (already
// in the kernel's format, per host_interface.hpp); top streams it to the AIE via
// hpwl_to_aie, reads the per-pin partials back over hpwl_from_aie, and writes
// them to DDR. This proves the host->PL->AIE->PL->host path in sw_emu.
//
// This is the single top-level kernel of the PL-centric design; per-iteration
// wiring of the other modules (memory_writer / density_manager / iteration_update
// / metrics) and the FFT pool are reintroduced incrementally. The v0 total-HPWL
// kernel is preserved in git history (it returns later via the metrics module).
//
// Stream <-> AIE PLIO connectivity is applied at link time from
// generate_link_cfg.py.

#include "host_interface.hpp"
#include "formats.hpp"
#include "modules/hpwl_manager.hpp"

using namespace plalgo;

extern "C" {
void top(
    const beat_t* hpwl_packet,   // DDR: host-built AIE input packet (4 floats/beat)
    beat_t*       hpwl_grad,     // DDR: AIE partials written back
    int           hpwl_in_beats, // 128b beats to stream to the AIE
    int           hpwl_out_beats,// 128b beats to read back from the AIE
    hls::stream<axis_t>& hpwl_to_aie,   // PL -> AIE HPWL graph
    hls::stream<axis_t>& hpwl_from_aie) // AIE HPWL graph -> PL
{
#pragma HLS INTERFACE m_axi port=hpwl_packet offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=hpwl_grad   offset=slave bundle=gmem1
#pragma HLS INTERFACE axis  port=hpwl_to_aie
#pragma HLS INTERFACE axis  port=hpwl_from_aie
// Each m_axi port's offset register and every scalar arg must share the one
// AXI-Lite "control" bundle in Vitis kernel mode (axis ports do not).
#pragma HLS INTERFACE s_axilite port=hpwl_packet    bundle=control
#pragma HLS INTERFACE s_axilite port=hpwl_grad      bundle=control
#pragma HLS INTERFACE s_axilite port=hpwl_in_beats  bundle=control
#pragma HLS INTERFACE s_axilite port=hpwl_out_beats bundle=control
#pragma HLS INTERFACE s_axilite port=return         bundle=control

    hpwl_manager(hpwl_packet, hpwl_grad, hpwl_in_beats, hpwl_out_beats,
                 hpwl_to_aie, hpwl_from_aie);
}
} // extern "C"
