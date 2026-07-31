/**
 * @file Grid.h
 * @brief The density grid: the die partitioned into bins_per_row x bins_per_col Bin cells,
 *        over which the electrostatic density/overflow are computed.
 */
#pragma once

#include "Common.h"
#include "Bin.h"
#include "Node.h"
//#include "Logger.h"

AIEPLACE_NAMESPACE_BEGIN

/// @brief A node's density footprint: the rectangle [xl,xh) x [yl,yh) its area is spread over,
///        and the area-conserving weight applied to each deposit.
struct NodeFootprint
{
    float xl, yl, xh, yh;
    float weight;
};

/// @brief Everything computeNodeFootprint needs besides the node. Built by Grid::footprintConfig()
///        so the bin scatter and the overflow metric cannot drift apart.
struct FootprintConfig
{
    float bin_w = 1.0f,  bin_h = 1.0f;
    float grid_w = 1.0f, grid_h = 1.0f;
    bool  clamp = true;         // sqrt(2) density smoothing (config enable_density_clamp)
    bool  macro_target_density_weight = false; // TODO #11b experiment; see below
    float target_density = 1.0f;
};

/**
 * @brief The one definition of density footprint geometry — shared by the bin scatter
 *        (Grid::computeBinOverlaps) and the overflow metric (Placer::computeOverflow), which
 *        MUST agree or the metric stops measuring the field the optimizer actually minimizes.
 *
 * When cfg.clamp is set, the footprint is inflated to at least sqrt(2) bins per dimension and
 * weighted by real_area/clamped_area (XPlace's expand_ratio, database.py:915-918), so a sub-bin
 * cell is smeared to grid resolution instead of spiking one bin — that smoothing is what keeps the
 * electrostatic gradient free of sub-bin spikes. Macros already exceed the clamp, so weight stays 1.
 *
 * The footprint is centered on the cell. XPlace stores node_pos as the CENTRE and forms
 * node_pos +/- size/2 (density_map_cuda_kernel.cu); sw_only stores the lower-left, so the same
 * centre is reconstructed as pos + size/2 and the expanded box hung symmetrically around it.
 *
 * FIXED terminals are never shifted in-die: IO pads and blockages sit in the margin outside the
 * core-row die, and shifting would pile their area onto the edge bins (a false density moat). They
 * are geometrically clipped by the caller's bin-range intersection instead — which is exactly what
 * XPlace does (density_map_cuda_naive_kernel.cu clamps x_l/x_h to the die, it does not translate).
 *
 * Mirrors the PL implementation in pl/src/pl_algo/src/modules/node_footprint.hpp.
 */
NodeFootprint computeNodeFootprint(Node* node_p, const FootprintConfig& cfg);

class Grid
{
private:
    // Grid data members
    Box m_die_area;
    int m_bins_per_row;
    int m_bins_per_col;
    float m_bin_width, m_bin_height;
    bool m_clamp_density = true; // clamp sub-bin cells to >= sqrt(2) bins (area-conserving)
    bool m_macro_target_density_weight = false; // see FootprintConfig::macro_target_density_weight
    float m_target_density = 1.0f;              // only used by the above experiment
    std::vector<std::vector<Bin> > m_bins; // 2D grid of bins to compute eField

public:
    // Constructors
    Grid() {}

    Grid(Box die_area) : m_die_area(die_area), m_bins_per_row(1024), m_bins_per_col(1024) { init(); }

    Grid(Box die_area, int bins_per_row, int bins_per_col) : 
        m_die_area(die_area), m_bins_per_row(bins_per_row), m_bins_per_col(bins_per_col) { init(); }

    void init();

    // How to quickly find the Bin that a Node is in?
    Bin& getBin(int row, int col) { return m_bins[row][col]; }

    int getBinsPerRow() { return m_bins_per_row; }
    int getBinsPerCol() { return m_bins_per_col; }
    int getDieWidth() { return m_die_area.getXsize(); }
    int getDieHeight() { return m_die_area.getYsize(); }
    float getBinWidth() { return m_bin_width; }
    float getBinHeight() { return m_bin_height; }
    void setClampDensity(bool clamp) { m_clamp_density = clamp; }
    void setMacroTargetDensityWeight(bool on) { m_macro_target_density_weight = on; }
    void setTargetDensity(float target_density) { m_target_density = target_density; }

    /// @brief Footprint geometry + policy for this grid. @p clamp is passed explicitly because
    ///        computeOverflow evaluates both the smoothed (clamp) and exact (no clamp) variants.
    FootprintConfig footprintConfig(bool clamp) const
    {
        FootprintConfig cfg;
        cfg.bin_w  = m_bin_width;   cfg.bin_h  = m_bin_height;
        cfg.grid_w = m_bins_per_row * m_bin_width;
        cfg.grid_h = m_bins_per_col * m_bin_height;
        cfg.clamp  = clamp;
        cfg.macro_target_density_weight = m_macro_target_density_weight;
        cfg.target_density = m_target_density;
        return cfg;
    }
    FootprintConfig footprintConfig() const { return footprintConfig(m_clamp_density); }

    void iterationReset();

    // The density deposit in two halves: the geometry, which touches only the node and so is
    // always safe to thread, and the scatter into shared bins, which is the reduction.
    void computeNodeOverlaps(Node* node_p, bool deposit_atomically);
    void depositNodeOverlaps(Node* node_p);
    void clampFixedDensity(float target_density);

    std::vector< std::vector<float> > getBinDensities(); // rho = overlap / bin_area
    std::vector< std::vector<float> > get_a_uv();


    // Returns normalized overflow in [0, 1]: total excess cell area above target_density
    // across all bins, divided by total movable cell area (fillers excluded).
    // Equivalent to Xplace's overflow metric; convergence target is typically ~0.07.
    float computeTotalOverflow(float target_density, float total_movable_area);

    // Print Functions
    void printOverflows();
    void printElectricFields();

};

AIEPLACE_NAMESPACE_END
