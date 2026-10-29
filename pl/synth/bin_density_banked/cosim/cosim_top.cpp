// cosim_top.cpp -- banked_top for RTL co-simulation: the same clear / deposit_region / write_out as
// ../banked_top.cpp, under its own name, with every m_axi `depth=` sized to cosim_tb.cpp's buffers (the
// production depths would make co-sim dump 1M-word memories). Build with -DPL_DENSITY_GRID_MAX=64. Meow.

#include "../banked_top.cpp"

#define COSIM_SLOTS_MAX 1024   // cosim_tb.cpp's per-producer slice buffer
#define COSIM_TABLE_MAX 16

extern "C" void banked_cosim_top(PRODUCER_ARGS(0), PRODUCER_ARGS(1), PRODUCER_ARGS(2), PRODUCER_ARGS(3),
                                 PRODUCER_ARGS(4), PRODUCER_ARGS(5), PRODUCER_ARGS(6), PRODUCER_ARGS(7),
                                 int num_footprints, int grid_bins, float bin_w, float bin_h, float fixed_scale,
                                 unsigned long long* rho_out) {
#pragma HLS INTERFACE m_axi port=footprints0 bundle=gmem0 offset=slave depth=COSIM_TABLE_MAX
#pragma HLS INTERFACE m_axi port=slots0 bundle=gmem0 offset=slave depth=COSIM_SLOTS_MAX
#pragma HLS INTERFACE m_axi port=footprints1 bundle=gmem1 offset=slave depth=COSIM_TABLE_MAX
#pragma HLS INTERFACE m_axi port=slots1 bundle=gmem1 offset=slave depth=COSIM_SLOTS_MAX
#pragma HLS INTERFACE m_axi port=footprints2 bundle=gmem2 offset=slave depth=COSIM_TABLE_MAX
#pragma HLS INTERFACE m_axi port=slots2 bundle=gmem2 offset=slave depth=COSIM_SLOTS_MAX
#pragma HLS INTERFACE m_axi port=footprints3 bundle=gmem3 offset=slave depth=COSIM_TABLE_MAX
#pragma HLS INTERFACE m_axi port=slots3 bundle=gmem3 offset=slave depth=COSIM_SLOTS_MAX
#pragma HLS INTERFACE m_axi port=footprints4 bundle=gmem4 offset=slave depth=COSIM_TABLE_MAX
#pragma HLS INTERFACE m_axi port=slots4 bundle=gmem4 offset=slave depth=COSIM_SLOTS_MAX
#pragma HLS INTERFACE m_axi port=footprints5 bundle=gmem5 offset=slave depth=COSIM_TABLE_MAX
#pragma HLS INTERFACE m_axi port=slots5 bundle=gmem5 offset=slave depth=COSIM_SLOTS_MAX
#pragma HLS INTERFACE m_axi port=footprints6 bundle=gmem6 offset=slave depth=COSIM_TABLE_MAX
#pragma HLS INTERFACE m_axi port=slots6 bundle=gmem6 offset=slave depth=COSIM_SLOTS_MAX
#pragma HLS INTERFACE m_axi port=footprints7 bundle=gmem7 offset=slave depth=COSIM_TABLE_MAX
#pragma HLS INTERFACE m_axi port=slots7 bundle=gmem7 offset=slave depth=COSIM_SLOTS_MAX
#pragma HLS INTERFACE m_axi port=rho_out bundle=gmem8 offset=slave depth=4096
    static_assert(BANK_WORDS * NUM_BANKS == 4096, "rho_out depth assumes PL_DENSITY_GRID_MAX=64");
#ifdef __SYNTHESIS__
    acc_word rho_URAM[NUM_BANKS][BANK_WORDS];
#else
    static acc_word rho_URAM[NUM_BANKS][BANK_WORDS];
#endif
#pragma HLS ARRAY_PARTITION variable=rho_URAM complete dim=1
#pragma HLS BIND_STORAGE variable=rho_URAM type=ram_2p impl=uram latency=3
clear:
    for (int a = 0; a < BANK_WORDS; a++) {
#pragma HLS PIPELINE II=1
        for (int b = 0; b < NUM_BANKS; b++) rho_URAM[b][a] = 0;
    }
    deposit_region(footprints0, slots0, num_slots0, footprints1, slots1, num_slots1, footprints2, slots2, num_slots2,
                   footprints3, slots3, num_slots3, footprints4, slots4, num_slots4, footprints5, slots5, num_slots5,
                   footprints6, slots6, num_slots6, footprints7, slots7, num_slots7, num_footprints, grid_bins, bin_w,
                   bin_h, fixed_scale, rho_URAM);
write_out:
    for (int i = 0; i < BANK_WORDS * NUM_BANKS; i++) {
#pragma HLS PIPELINE II=1
        rho_out[i] = (unsigned long long)rho_URAM[i % NUM_BANKS][i / NUM_BANKS];
    }
}
