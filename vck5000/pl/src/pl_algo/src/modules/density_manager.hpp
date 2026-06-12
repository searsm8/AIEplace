#ifndef PL_ALGO_DENSITY_MANAGER_HPP
#define PL_ALGO_DENSITY_MANAGER_HPP

// Density Manager module (black box for v1).
//
// Role: compute the electrostatic field from node positions. This is the heart of the
// PL-centric rework: the AIE does ONLY the FFT; the Density Manager performs the
// DCT/IDCT(/IDXST) pre- and post-processing in PL (reorder + twiddle ROM + sign/Re),
// holds the transform FSM, and owns the 1024x1024 row-tile staging through DDR.
//
// Flow (per the diagram): bin density -> PL preprocess -> AIE FFT pool (FFT_LANES rows)
//                         -> PL postprocess -> Ex, Ey.
// IDXST is deferred for v1 (a tweak on the IDCT flow); Ey approximated via IDCT path
// until added.
//
// Inputs:
//   coords       : DDR node coordinates (read) -> binned into density
//   num_nodes    : movable node count
//   fft_from_aie : FFT results from the AIE pool (cfloat beats, FFT_LANES lanes)
// Outputs:
//   bin_density  : DDR scratch, 1024x1024 real (4 MB)
//   efield_x     : DDR Ex field, 1024x1024 real (4 MB)
//   efield_y     : DDR Ey field, 1024x1024 real (4 MB)
//   fft_to_aie   : PL-preprocessed rows streamed to the AIE FFT pool (FFT_LANES lanes)

#include "../formats.hpp"

namespace plalgo {

static void density_manager(const beat_t* coords,
                            beat_t* bin_density,
                            beat_t* efield_x,
                            beat_t* efield_y,
                            int num_nodes,
                            hls::stream<axis_t> fft_to_aie[FFT_LANES],
                            hls::stream<axis_t> fft_from_aie[FFT_LANES]) {
    // TODO: bin density (scatter nodes), PL preprocess, drive FFT pool, PL postprocess.
    // Transform FSM (TF_DCT -> TF_IDCT -> [TF_IDXST]) sequences the passes here.
    // Stub: zero the field matrices so the m_axi ports are live and well-defined.
clear_fields:
    for (int b = 0; b < N_BINS / FLOATS_PER_BEAT; b++) {
        bin_density[b] = beat_t(0);
        efield_x[b]    = beat_t(0);
        efield_y[b]    = beat_t(0);
    }
    (void)coords;
    (void)num_nodes;
    (void)fft_to_aie;
    (void)fft_from_aie;
}

} // namespace plalgo

#endif // PL_ALGO_DENSITY_MANAGER_HPP
