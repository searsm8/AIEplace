// density.cpp -- sw_only's Density.cpp and the Grid.cpp pieces it calls: the footprint deposit, the
// fixed-density cap, the DCT field solve, the force gather, and the overflow metric. Meow.

#include "placer.hpp"

#include "DCT.h"

#include <algorithm>
#include <cmath>

namespace sw_flat {

namespace {

struct Footprint { float xl, yl, xh, yh, weight; };

// computeNodeFootprint (Grid.cpp): the sqrt(2)-bin clamp with an area-conserving weight, centred on
// the cell; a movable macro below td = 1 deposits at weight td. Meow.
inline Footprint node_footprint(const Placer& placer, float x, float y, float w, float h, bool is_macro, bool clamp)
{
    float cw = w, ch = h, weight = 1.0f;
    if (clamp) {
        cw = std::max(w, placer.bin_w * (float)M_SQRT2);
        ch = std::max(h, placer.bin_h * (float)M_SQRT2);
        weight = (cw > 0.0f && ch > 0.0f) ? (w * h) / (cw * ch) : 0.0f;
        if (placer.cfg.target_density < 1.0f && is_macro) weight = placer.cfg.target_density;
    }
    float xl = x + 0.5f * w - 0.5f * cw;
    float yl = y + 0.5f * h - 0.5f * ch;
    return Footprint{xl, yl, xl + cw, yl + ch, weight};
}

// Grid::computeNodeOverlaps' loop: visit(bin, area) in its order (column, then row). The deposit
// and the force gather both walk it, so the gather sums in the order the bins were filled. Meow.
template <typename Visit>
inline void for_each_overlap(const Placer& placer, const Footprint& fp, Visit&& visit)
{
    const int bins = placer.bins;
    int col_lo = std::max(0, (int)(fp.xl / placer.bin_w));
    int col_hi = std::min(bins - 1, (int)(fp.xh / placer.bin_w));
    int row_lo = std::max(0, (int)(fp.yl / placer.bin_h));
    int row_hi = std::min(bins - 1, (int)(fp.yh / placer.bin_h));
    for (int col = col_lo; col <= col_hi; col++) {
        float bin_xl = col * placer.bin_w;
        float overlap_w = std::min(fp.xh, bin_xl + placer.bin_w) - std::max(fp.xl, bin_xl);
        if (overlap_w <= 0.0f) continue;
        for (int row = row_lo; row <= row_hi; row++) {
            float bin_yl = row * placer.bin_h;
            float overlap_h = std::min(fp.yh, bin_yl + placer.bin_h) - std::max(fp.yl, bin_yl);
            if (overlap_h <= 0.0f) continue;
            float overlap_area = overlap_w * overlap_h * fp.weight;
            visit((size_t)col * bins + row, overlap_area);
        }
    }
}

inline Footprint slot_footprint(const Placer& placer, long slot, bool clamp)
{
    const Position& probe = placer.next.probe_pos[slot];
    return node_footprint(placer, probe.x, probe.y, placer.layout.width[slot], placer.layout.height[slot],
                          placer.is_movable_macro[slot], clamp);
}

inline Footprint fixed_footprint(const Placer& placer, int node, bool clamp)
{
    const FlatDesign& d = placer.design;
    return node_footprint(placer, d.node_x[node], d.node_y[node], d.node_width[node], d.node_height[node], false, clamp);
}

} // namespace

// computeOverlaps + computeElectricFields_DCT. The deposit is one serial pass in sw_only's order:
// fixed, the cap, then deposit_slots (sw_only's order unless [sw_flat] slot_order has "deposit"). Meow.
void Placer::compute_electric_fields()
{
    PhaseTimer timer{time_field};
    std::fill(bin_overlap.begin(), bin_overlap.end(), 0.0f);
    auto deposit = [&](size_t bin, float overlap_area) { bin_overlap[bin] += overlap_area; };
    for (int node : fixed_nodes) for_each_overlap(*this, fixed_footprint(*this, node, cfg.enable_density_clamp), deposit);
    // Grid::clampFixedDensity: min(rho, td), the cap (CLAUDE.md, deliberate divergences). Meow.
    for (size_t bin = 0; bin < bin_overlap.size(); bin++)
        bin_overlap[bin] = std::min(bin_overlap[bin], bin_area[bin] * cfg.target_density);
    for (long slot : deposit_slots) for_each_overlap(*this, slot_footprint(*this, slot, cfg.enable_density_clamp), deposit);

    // compute_a_uv_DCT, verbatim: Grid::getBinDensities is [col][row], scaled by bin [0][0]'s area. Meow.
    const int num_rows = bins, num_cols = bins;
    std::vector<std::vector<float>> density(bins, std::vector<float>(bins));
    float bin_area_inv = 1 / bin_area[0];
    #pragma omp parallel for schedule(static)
    for (int col = 0; col < bins; col++)
        for (int row = 0; row < bins; row++) density[col][row] = bin_overlap[(size_t)col * bins + row] * bin_area_inv;

    std::vector<std::vector<float>> temp(num_rows, std::vector<float>(num_cols));
    std::vector<std::vector<float>> a_uv(num_rows, std::vector<float>(num_cols));
    #pragma omp parallel for schedule(static)
    for (int row_index = 0; row_index < num_rows; row_index++)
        AIEplace::DCT_fft(density[row_index].data(), temp[row_index].data(), num_cols, cfg.dct_normalize);
    temp = AIEplace::transpose(temp);
    #pragma omp parallel for schedule(static)
    for (int col_index = 0; col_index < num_cols; col_index++)
        AIEplace::DCT_fft(temp[col_index].data(), a_uv[col_index].data(), num_rows, cfg.dct_normalize);
    a_uv = AIEplace::transpose(a_uv);

    // compute_eField_DCT, verbatim: the inverse transforms are unnormalized (field-faithful). Meow.
    std::vector<std::vector<float>> Ex(num_rows, std::vector<float>(num_cols));
    std::vector<std::vector<float>> Ey(num_rows, std::vector<float>(num_cols));
    Ex[0][0] = 0; Ey[0][0] = 0;
    #pragma omp parallel for schedule(static)
    for (int u = 0; u < num_rows; u++) {
        for (int v = 0; v < num_cols; v++) {
            if (u == 0 && v == 0) continue;
            float w_u = 2*M_PI*u / num_cols;
            float w_v = 2*M_PI*v / num_rows;
            float denom = w_u*w_u + w_v*w_v;
            Ex[u][v] = a_uv[u][v] * w_u / denom;
            Ey[u][v] = a_uv[u][v] * w_v / denom;
        }
    }
    #pragma omp parallel for schedule(static)
    for (int row_index = 0; row_index < num_rows; row_index++) {
        AIEplace::IDCT_fft (Ex[row_index].data(), Ex[row_index].data(), num_cols);
        AIEplace::IDXST_fft(Ey[row_index].data(), Ey[row_index].data(), num_cols);
    }
    Ex = AIEplace::transpose(Ex); Ey = AIEplace::transpose(Ey);
    #pragma omp parallel for schedule(static)
    for (int row_index = 0; row_index < num_rows; row_index++) {
        AIEplace::IDXST_fft(Ex[row_index].data(), Ex[row_index].data(), num_cols);
        AIEplace::IDCT_fft (Ey[row_index].data(), Ey[row_index].data(), num_cols);
    }
    Ex = AIEplace::transpose(Ex); Ey = AIEplace::transpose(Ey);
    #pragma omp parallel for schedule(static)
    for (int col = 0; col < bins; col++)
        for (int row = 0; row < bins; row++) efield[(size_t)col * bins + row] = Gradient(Ex[col][row], Ey[col][row]);
}

// computeElectrostaticForce: sum over the footprint's bins of lambda * overlap * E. sw_only keeps the
// overlap as a double holding a float, so its coeff is one rounding of the exact product, which is
// what the float multiply gives. The footprint is recomputed: the probe has not moved since the
// deposit. Meow.
Gradient Placer::electrostatic_force(long slot) const
{
    Gradient electro_force;
    for_each_overlap(*this, slot_footprint(*this, slot, cfg.enable_density_clamp), [&](size_t bin, float overlap) {
        float coeff = density_weight * 1.0f * overlap;   // 1.0f: Bin::local_density_weight
        electro_force += coeff * efield[bin];
    });
    return electro_force;
}

// computeOverflow(smooth) with no fillers and no macro exclusion, the only form the phase-1 path uses:
// the movable components over a fixed baseline at EXACT size, capped at bin_w * bin_h * td. Its
// overlap form rounds differently from the deposit's ((c+1)*bin_w, not bin_xl + bin_w), so it is
// written out again. Meow.
float Placer::compute_overflow(bool smooth) const
{
    const int nx = bins, ny = bins;
    const float bin_area_metric = bin_w * bin_h;
    const float cap = bin_area_metric * cfg.target_density;
    std::vector<float> density((size_t)nx * ny, 0.0f);

    auto deposit = [&](const Footprint& fp) {
        int col_lo = std::max(0, (int)(fp.xl / bin_w));
        int col_hi = std::min(nx - 1, (int)(fp.xh / bin_w));
        int row_lo = std::max(0, (int)(fp.yl / bin_h));
        int row_hi = std::min(ny - 1, (int)(fp.yh / bin_h));
        for (int c = col_lo; c <= col_hi; c++) {
            float ox = std::min(fp.xh, (c + 1) * bin_w) - std::max(fp.xl, c * bin_w);
            if (ox <= 0.0f) continue;
            for (int r = row_lo; r <= row_hi; r++) {
                float oy = std::min(fp.yh, (r + 1) * bin_h) - std::max(fp.yl, r * bin_h);
                if (oy <= 0.0f) continue;
                density[(size_t)c * ny + r] += ox * oy * fp.weight;
            }
        }
    };
    for (int node : fixed_nodes) deposit(fixed_footprint(*this, node, false));
    for (float& d : density) d = std::min(d, bin_area_metric * cfg.target_density);   // capFixedDensity
    for (int i = 0; i < filler_start; i++) deposit(slot_footprint(*this, deposit_slots[i], smooth));

    float overflow_area = 0.0f;
    for (float d : density) overflow_area += std::max(0.0f, d - cap);
    return overflow_area / (total_movable_area + 1e-8f);
}

} // namespace sw_flat
