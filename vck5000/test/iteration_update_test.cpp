// Verify iteration_update.hpp -- the ePlace Nesterov position update, and its DATAFLOW partner
// memory_writer. One call = one step mirroring three sw_only functions back to back:
//   combineGradients()   : g_total = g_wl - lambda * g_density
//   Node::step()         : precondition, u_{k+1} = v_k - alpha*P*g_total,
//                          then v_{k+1} = u_{k+1} + coeff*(u_{k+1} - u_k)
//   enforceDieBoundaries : clamp BOTH u_{k+1} and v_{k+1} into [0, die - size]
// (sign is `-`: the eField sign convention bakes Xplace's `+=` into the field; no per-bin
// local_density_weight -- see pl_algo_5c_algo_audit.)
//
// FOUR ASSERTIONS:
//   [1] U_OUT     -- committed u_{k+1} vs a double re-derivation of the whole chain. rel_rms.
//   [2] V_OUT     -- the streamed look-ahead v_{k+1} vs the same golden (adds the momentum term).
//   [3] CLAMP     -- nodes driven hard past each of the four die edges must land EXACTLY on the
//                    bound (0 or die-size), bit-exact, for both u and v. clampf returns the bound
//                    float, so this is exact; a dropped clamp or wrong bound is caught here.
//   [4] WRITER    -- feeding iteration_update's v stream into memory_writer reproduces those v
//                    values in the coords buffer, in order, BIT-EXACT (the single-writer contract).
//
// Branches driven: precond == 1 and > 1 (inv_p == 1 vs < 1); coeff == 0 (warm-up, v == u) mixed
// with coeff > 0; lambda > 0 so the density term actually contributes.

#include "tier1_stub.hpp"                  // PL_TIER1_STUB + hls::stream stand-in (used below)
#include "modules/iteration_update.hpp"    // the real module: iteration_update, memory_writer
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace plalgo;                    // coord_t, NodeBox, iteration_update, memory_writer

static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

int main() {
    const float die_xmax = 10000.0f, die_ymax = 8000.0f;
    const float lambda = 1.4f, alpha = 0.05f, coeff = 0.37f;

    std::mt19937 rng(20260828u);
    std::uniform_real_distribution<float> gd(-50.0f, 50.0f);     // gradients, both signs
    std::uniform_real_distribution<float> sz(2.0f, 60.0f);       // cell size
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    const int M = 6000;
    std::vector<coord_t> g_hpwl(M), g_density(M), u_in(M);
    std::vector<NodeBox> node_box(M);
    std::vector<float>   precond(M);

    for (int n = 0; n < M; n++) {
        float w = sz(rng), h = sz(rng);
        float vkx = u01(rng) * (die_xmax - w), vky = u01(rng) * (die_ymax - h);   // v_k inside die
        node_box[n] = { vkx, vky, w, h };
        u_in[n]     = { vkx + gd(rng) * 0.1f, vky + gd(rng) * 0.1f };             // u_k near v_k
        g_hpwl[n]   = { gd(rng), gd(rng) };
        g_density[n]= { gd(rng), gd(rng) };
        precond[n]  = (n & 1) ? 1.0f : (1.0f + u01(rng) * 40.0f);                 // mix inv_p==1 and <1
        if ((n % 500) == 0) { g_hpwl[n] = { 0.0f, 0.0f }; g_density[n] = { 0.0f, 0.0f }; } // no-move node
    }
    // Designed clamp cases: gigantic gradient shoves the node hard past one edge (both u and v).
    // Indices reserved at the tail so they don't perturb the random population above.
    struct EdgeCase { int idx; float gx, gy; };
    const float BIG = 1e6f;
    EdgeCase edges[] = {
        { 10, -BIG,  0.0f }, { 20,  BIG,  0.0f },   // -x (ux>die via v_k-alpha*(-)), +x
        { 30,  0.0f, -BIG }, { 40,  0.0f,  BIG },   // -y, +y
        { 50, -BIG, -BIG  }, { 60,  BIG,  BIG  },   // corners
    };
    for (const EdgeCase& e : edges) {
        precond[e.idx] = 1.0f;                       // inv_p == 1 so the shove is undiluted
        g_hpwl[e.idx]  = { e.gx, e.gy };
        g_density[e.idx] = { 0.0f, 0.0f };
    }

    // Warm-up (coeff == 0) mixed in for a subset: re-run those by zeroing coeff per node is not how
    // the module works (coeff is a scalar), so instead verify coeff==0 in a SECOND full call below.

    // ---- run the real module (coeff > 0) ----
    hls::stream<coord_t> v_stream;
    std::vector<coord_t> u_out(M, coord_t{ -7.7e30f, -7.7e30f });
    iteration_update(g_hpwl.data(), g_density.data(), node_box.data(), u_in.data(), precond.data(),
                     u_out.data(), lambda, alpha, coeff, die_xmax, die_ymax, M, v_stream);
    std::vector<coord_t> v_out(M);
    for (int n = 0; n < M; n++) v_out[n] = v_stream.read();      // FIFO -> node order 0..M-1

    // Re-stream v for the memory_writer check (v_stream is now drained).
    hls::stream<coord_t> v_stream2;
    for (int n = 0; n < M; n++) v_stream2.write(v_out[n]);
    std::vector<coord_t> coords(M, coord_t{ 0, 0 });
    memory_writer(coords.data(), v_stream2, M);

    // ---- golden (double) + assertions ----
    double su = 0, sv = 0, sr = 0;
    int clamp_bad = 0, writer_bad = 0;
    std::vector<int> is_edge(M, 0);
    for (const EdgeCase& e : edges) is_edge[e.idx] = 1;

    for (int n = 0; n < M; n++) {
        const double gx = (double)g_hpwl[n].x - (double)lambda * g_density[n].x;
        const double gy = (double)g_hpwl[n].y - (double)lambda * g_density[n].y;
        const double inv_p = 1.0 / (double)precond[n];
        const double vkx = node_box[n].x, vky = node_box[n].y;
        const double ux = vkx - (double)alpha * gx * inv_p;
        const double uy = vky - (double)alpha * gy * inv_p;
        const double vx = ux + (double)coeff * (ux - (double)u_in[n].x);
        const double vy = uy + (double)coeff * (uy - (double)u_in[n].y);
        const double mx = (double)die_xmax - node_box[n].w;      // float bound, promoted
        const double my = (double)die_ymax - node_box[n].h;
        const double uox = clampd(ux, 0.0, mx), uoy = clampd(uy, 0.0, my);
        const double vox = clampd(vx, 0.0, mx), voy = clampd(vy, 0.0, my);

        const double eux = (double)u_out[n].x - uox, euy = (double)u_out[n].y - uoy;
        const double evx = (double)v_out[n].x - vox, evy = (double)v_out[n].y - voy;
        su += eux * eux + euy * euy;
        sv += evx * evx + evy * evy;
        sr += uox * uox + uoy * uoy;

        if (is_edge[n]) {
            // The shoved axis must sit exactly on a bound. clampf returns the float bound value.
            const float fmx = die_xmax - node_box[n].w, fmy = die_ymax - node_box[n].h;
            auto on_bound = [](float v, float hi) { return v == 0.0f || v == hi; };
            if (g_hpwl[n].x != 0.0f && !(on_bound(u_out[n].x, fmx) && on_bound(v_out[n].x, fmx))) clamp_bad++;
            if (g_hpwl[n].y != 0.0f && !(on_bound(u_out[n].y, fmy) && on_bound(v_out[n].y, fmy))) clamp_bad++;
        }
        if (coords[n].x != v_out[n].x || coords[n].y != v_out[n].y) writer_bad++;
    }
    const double u_rel = std::sqrt(su / sr), v_rel = std::sqrt(sv / sr);

    // ---- second call: coeff == 0 warm-up => v_out must equal u_out exactly (before clamp they are
    // equal; clamp bounds are identical, so post-clamp they stay equal). ----
    hls::stream<coord_t> vs0;
    std::vector<coord_t> u0(M);
    iteration_update(g_hpwl.data(), g_density.data(), node_box.data(), u_in.data(), precond.data(),
                     u0.data(), lambda, alpha, 0.0f, die_xmax, die_ymax, M, vs0);
    int warmup_bad = 0;
    for (int n = 0; n < M; n++) { coord_t v = vs0.read(); if (v.x != u0[n].x || v.y != u0[n].y) warmup_bad++; }

    bool ok = true;
    const double TOL = 1e-6;   // double-vs-float per-node arithmetic; observed below
    if (!(u_rel < TOL)) { printf("FAIL [1] u_out rel_rms=%.3e (tol %.0e)\n", u_rel, TOL); ok = false; }
    if (!(v_rel < TOL)) { printf("FAIL [2] v_out rel_rms=%.3e (tol %.0e)\n", v_rel, TOL); ok = false; }
    if (clamp_bad)  { printf("FAIL [3] %d edge nodes not clamped onto a bound\n", clamp_bad); ok = false; }
    if (writer_bad) { printf("FAIL [4] memory_writer mismatched %d/%d coords\n", writer_bad, M); ok = false; }
    if (warmup_bad) { printf("FAIL [warmup] coeff==0 gave v != u for %d nodes\n", warmup_bad); ok = false; }

    printf("[1] u_out      rel_rms=%.3e (tol %.0e)   [%d movable]\n", u_rel, TOL, M);
    printf("[2] v_out      rel_rms=%.3e (tol %.0e)\n", v_rel, TOL);
    printf("[3] clamp      %d edge cases (4 edges + 2 corners), %d off-bound\n", (int)(sizeof(edges)/sizeof(edges[0])), clamp_bad);
    printf("[4] writer     %d/%d coords match streamed v\n", M - writer_bad, M);
    printf("[info] warm-up coeff==0: v==u for %d/%d nodes\n", M - warmup_bad, M);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
