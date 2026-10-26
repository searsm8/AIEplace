// hpwl_gradient_xrt -- #45: hpwl_gradient_CU as a Vitis kernel (pl/ `make xclbin`), driven through XRT, vs
// the same CU run in g++ on the same inputs. BIT FOR BIT.
//
//   hpwl_gradient_xrt <kernel.xclbin> <config.toml> [iterations=30]
//   hpwl_gradient_xrt <kernel.xclbin> synthetic [capacity=1536]
//
// A config must turn on the PL arithmetic (wa_sums = "tree", grad_merge = "stream", hpwl = "fixed",
// wa_exp = "lut"; test/hpwl_gradient_xrt.sh appends them). Positions: the initial placement plus
// `iterations` steps, as in record_stream_check. `synthetic` is the RTL co-sim's design instead
// (pl/synth/hpwl_gradient_CU/cosim/cosim_tb.cpp: ~1200 cells, 16 chunks at 1536), small enough for
// hw_emu, where a real benchmark is hours of RTL simulation. Then, per axis, one kernel run:
//   [1] IMAGE     the device's whole grad image equals the g++ CU's, bit for bit (both start NaN, so
//                 a slot the device never writes cannot pass)
//   [2] HPWL      the device's hpwl_total_fixed equals the g++ CU's
//   [3] GOLDEN    (config only) every OWNED slot equals sw_flat's own gradient: record_stream_check's
//                 claim, carried across the device path
// Emulation (sw_emu / hw_emu) is chosen by XCL_EMULATION_MODE, as for any XRT host. Meow.

#include "placer/placer.hpp"

#include "../../../pl/test/tier1_stub.hpp"   // PL_TIER1_STUB + hls::stream stand-in, BEFORE the module
#include "modules/hpwl_gradient_CU.hpp"
#include "../../../pl/test/wa_gradient_golden.hpp"

#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_kernel.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// Everything one kernel call reads, in the form both the g++ CU and the device take. Meow.
struct AxisInputs {
    packer::ChunkedDevice dev;
    std::vector<pinrec::RecordBeat> records;
    std::vector<pinrec::SlotBeat> pos;
    std::vector<float> exp_lut;
    float inv_gamma = 0, inv_lut_step = 0;
};

struct AxisResult {
    std::vector<pinrec::SlotBeat> grad;
    int64_t hpwl_fixed = -1;
    double kernel_ms = 0;
};

AxisInputs make_inputs(packer::ChunkedDevice dev, std::vector<float> exp_lut, float inv_gamma, float inv_lut_step)
{
    AxisInputs in;
    in.dev = std::move(dev);
    in.records.resize(in.dev.records.size() / pinrec::LANES);
    std::memcpy(in.records.data(), in.dev.records.data(), in.dev.records.size() * sizeof(uint32_t));
    in.pos.resize(in.dev.slot_images.size() / pinrec::LANES);
    std::memcpy(in.pos.data(), in.dev.slot_images.data(), in.dev.slot_images.size() * sizeof(float));
    in.exp_lut = std::move(exp_lut);
    in.inv_gamma = inv_gamma;
    in.inv_lut_step = inv_lut_step;
    return in;
}

AxisInputs placer_inputs(const sw_flat::Placer& placer, int axis)
{
    std::vector<float> node_pos(placer.design.num_nodes());
    for (int node = 0; node < placer.design.num_nodes(); node++) {
        const long slot = placer.layout.home_slot[node];
        const sw_flat::Position p = slot < 0 ? sw_flat::Position(placer.design.node_x[node], placer.design.node_y[node])
                                             : placer.next.probe_pos[slot];
        node_pos[node] = axis ? p.y : p.x;
    }
    return make_inputs(packer::chunked_device_arrays(placer.chunked, axis, node_pos), placer.exp_lut, placer.inv_gamma,
                       1.0f / (sw_flat::PL_LUT_STEP_NORM * placer.gamma));
}

std::vector<pinrec::SlotBeat> nan_image(size_t beats)
{
    std::vector<pinrec::SlotBeat> grad(beats);
    for (auto& beat : grad) for (float& v : beat.v) v = NAN;
    return grad;
}

AxisResult run_gpp(AxisInputs in)   // by value: send_borrowed_pass writes pos
{
    AxisResult out;
    out.grad = nan_image(in.pos.size());
    plalgo::hpwl_gradient_CU(in.dev.desc.data(), in.dev.group_ends.data(), (int)in.dev.desc.size(), in.records.data(),
                             in.pos.data(), in.dev.macro_pins.data(), in.dev.borrowed_blocks.data(), in.dev.borrowed_ops.data(),
                             in.dev.offset_table.data(), (int)in.dev.offset_table.size(), in.exp_lut.data(),
                             (int)in.exp_lut.size(), out.grad.data(), in.dev.offset_bits, in.inv_gamma, in.inv_lut_step,
                             out.hpwl_fixed);
    return out;
}

// One buffer per pointer argument, in the kernel's argument order. XRT rejects a zero-size bo, so an
// empty array (no macro pins, an unchunked design's borrowed blocks) gets a few words that the CU never reads. Meow.
template <typename T>
xrt::bo to_device(const xrt::device& device, const xrt::kernel& kernel, int arg, const std::vector<T>& host)
{
    const size_t bytes = std::max<size_t>(host.size() * sizeof(T), 64);
    xrt::bo bo(device, bytes, kernel.group_id(arg));
    if (!host.empty()) bo.write(host.data(), host.size() * sizeof(T), 0);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE);
    return bo;
}

AxisResult run_xrt(const xrt::device& device, xrt::kernel& kernel, const AxisInputs& in)
{
    AxisResult out;
    out.grad = nan_image(in.pos.size());
    xrt::bo chunks       = to_device(device, kernel, 0, in.dev.desc);
    xrt::bo group_ends   = to_device(device, kernel, 1, in.dev.group_ends);
    xrt::bo records      = to_device(device, kernel, 3, in.records);
    xrt::bo pos          = to_device(device, kernel, 4, in.pos);
    xrt::bo macro_pins   = to_device(device, kernel, 5, in.dev.macro_pins);
    xrt::bo borrowed_blocks  = to_device(device, kernel, 6, in.dev.borrowed_blocks);
    xrt::bo borrowed_ops     = to_device(device, kernel, 7, in.dev.borrowed_ops);
    xrt::bo offset_table = to_device(device, kernel, 8, in.dev.offset_table);
    xrt::bo exp_lut      = to_device(device, kernel, 10, in.exp_lut);
    xrt::bo grad         = to_device(device, kernel, 12, out.grad);
    xrt::bo hpwl_total   = to_device(device, kernel, 16, std::vector<int64_t>{-1});

    const auto start = std::chrono::steady_clock::now();
    xrt::run run = kernel(chunks, group_ends, (int)in.dev.desc.size(), records, pos, macro_pins, borrowed_blocks, borrowed_ops,
                          offset_table, (int)in.dev.offset_table.size(), exp_lut, (int)in.exp_lut.size(), grad,
                          in.dev.offset_bits, in.inv_gamma, in.inv_lut_step, hpwl_total);
    run.wait();
    out.kernel_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    grad.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    grad.read(out.grad.data(), out.grad.size() * sizeof(pinrec::SlotBeat), 0);
    hpwl_total.sync(XCL_BO_SYNC_BO_FROM_DEVICE);
    hpwl_total.read(&out.hpwl_fixed, sizeof out.hpwl_fixed, 0);
    return out;
}

// [1] and [2] for one axis; returns the device's grad image as floats for [3]. Meow.
std::vector<float> check_axis(const xrt::device& device, xrt::kernel& kernel, const AxisInputs& in, int axis, int& failures)
{
    printf("[info]        axis %d: %zu chunks, %zu record beats, %zu slot beats, %zu borrowed ops (the kernel's work)\n",
           axis, in.dev.desc.size(), in.records.size(), in.pos.size(), in.dev.borrowed_ops.size());
    const AxisResult golden = run_gpp(in);
    const AxisResult hw = run_xrt(device, kernel, in);

    const float* mine = reinterpret_cast<const float*>(golden.grad.data());
    const float* theirs = reinterpret_cast<const float*>(hw.grad.data());
    const long slots = (long)golden.grad.size() * pinrec::LANES;
    long differing = 0;
    for (long slot = 0; slot < slots; slot++) differing += std::memcmp(&mine[slot], &theirs[slot], sizeof(float)) != 0;
    printf("[1] IMAGE     axis %d: %ld of %ld slots differ from the g++ CU (need 0); kernel run %.1f ms\n",
           axis, differing, slots, hw.kernel_ms);
    failures += differing != 0;

    const bool same = hw.hpwl_fixed == golden.hpwl_fixed;
    printf("[2] HPWL      axis %d: device %lld, g++ CU %lld %s\n", axis, (long long)hw.hpwl_fixed,
           (long long)golden.hpwl_fixed, same ? "" : "MISMATCH");
    failures += !same;
    return std::vector<float>(theirs, theirs + slots);
}

int run_synthetic(const xrt::device& device, xrt::kernel& kernel, long capacity)
{
    fixture::SyntheticSpec spec;
    spec.cells = 1200; spec.macros = 5; spec.fixed = 20; spec.nets = 1140;
    const packer::Netlist nl = fixture::build_synthetic(20260923u, spec);
    const packer::Config cfg;
    const packer::Chunked ch = packer::encode_chunked(nl, cfg, capacity);
    if (packer::check_chunked(nl, ch, cfg)) { printf("FAIL chunked packer check\n"); return 1; }
    const golden::Lut lut = golden::make_lut();
    int failures = 0;
    for (int axis = 0; axis < 2; axis++) {
        const std::vector<float> node_pos = fixture::random_positions(nl.movable.size(), 11u + axis, golden::EXTENT);
        check_axis(device, kernel, make_inputs(packer::chunked_device_arrays(ch, axis, node_pos), lut.table,
                                               1.0f / golden::GAMMA, lut.inv_step), axis, failures);
    }
    printf("%s synthetic: %d chunks at capacity %ld, %zu nodes\n", failures ? "FAIL" : "PASS", ch.num_chunks, capacity,
           nl.movable.size());
    return failures ? 1 : 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: hpwl_gradient_xrt <kernel.xclbin> <config.toml> [iterations]\n"
                        "       hpwl_gradient_xrt <kernel.xclbin> synthetic [capacity]\n");
        return 2;
    }
    xrt::device device(0);
    const xrt::uuid uuid = device.load_xclbin(argv[1]);
    xrt::kernel kernel(device, uuid, "hpwl_gradient_CU_top");
    if (std::strcmp(argv[2], "synthetic") == 0) return run_synthetic(device, kernel, argc > 3 ? atol(argv[3]) : 1536);

    const int iterations = argc > 3 ? atoi(argv[3]) : 30;
    sw_flat::PlacerConfig cfg = sw_flat::read_placer_config(argv[2]);
    cfg.dump_schedule_trace = false;
    if (!(cfg.wa_sums_tree && cfg.grad_merge_stream && cfg.hpwl_fixed && cfg.wa_exp_lut)) {
        fprintf(stderr, "hpwl_gradient_xrt: the config must set wa_sums = \"tree\", grad_merge = \"stream\", hpwl = \"fixed\", wa_exp = \"lut\"\n");
        return 2;
    }
    sw_flat::Placer placer;
    placer.setup(cfg);
    placer.initialize_placement();
    placer.compute_hpwl_partials();
    placer.compute_electric_fields();
    placer.initialize_density_weight();
    while (placer.iteration < iterations && !placer.nan_detected) placer.perform_iteration();
    if (placer.nan_detected) { printf("FAIL %s: NaN before iteration %d\n", placer.design.name.c_str(), iterations); return 1; }
    placer.compute_hpwl_partials();   // the WA gradient alone: perform_iteration leaves the combined one

    int failures = 0;
    const std::vector<float> device_grad[2] = {check_axis(device, kernel, placer_inputs(placer, 0), 0, failures),
                                               check_axis(device, kernel, placer_inputs(placer, 1), 1, failures)};
    long owned = 0, differing = 0;
    for (long slot = 0; slot < placer.layout.first_filler_slot; slot++) {
        if (placer.layout.role[slot] != sw_flat::SlotRole::OWNED) continue;
        owned++;
        for (int axis = 0; axis < 2; axis++) {
            const float mine = axis ? placer.next.probe_grad[slot].y : placer.next.probe_grad[slot].x;
            differing += std::memcmp(&mine, &device_grad[axis][slot], sizeof mine) != 0;
        }
    }
    printf("[3] GOLDEN    %ld of %ld owned slots x 2 axes differ from sw_flat (need 0)\n", differing, owned);
    failures += differing != 0;

    printf("%s %s: %d chunks, %ld owned slots, iteration %d\n", failures ? "FAIL" : "PASS", placer.design.name.c_str(),
           placer.chunked.num_chunks, owned, placer.iteration);
    return failures ? 1 : 0;
}
