#ifndef GRID_H
#define GRID_H

#include "Common.h"
#include "Types.h"

namespace AIEPLACE_NAMESPACE {

  class Grid
  {
    public:
      // Constructors
      Grid() {}

      Grid(float width, float height, int bins_per_row, int bins_per_col) :
        die_area_(Box{width, height}),
        bins_per_row_(bins_per_row),
        bins_per_col_(bins_per_col)
      { init(); }

      Grid(Box die_area) : die_area_(die_area), bins_per_row_(1024), bins_per_col_(1024) { init(); }

      Grid(Box die_area, int bins_per_row, int bins_per_col) :
        die_area_(die_area), bins_per_row_(bins_per_row), bins_per_col_(bins_per_col) { init(); }

      void setDimensions(float width, float height, int bins_per_row, int bins_per_col);
      void init();

      // How to quickly find the Bin that a Node is in?
      Bin& getBin(int row, int col) { return density_bins_[binIndex(row, col)]; }

      int getBinsPerRow() { return bins_per_row_; }
      int getBinsPerCol() { return bins_per_col_; }
      float getDieWidth() { return die_area_.getXsize(); }
      float getDieHeight() { return die_area_.getYsize(); }
      float getDieArea() { return die_area_.getArea(); }
      float getBinWidth() { return bin_width_; }
      float getBinHeight() { return bin_height_; }

      void addComponentAreaToDensityBins(const Box& comp);
      void clearDensities();

      // Returns normalized overflow in [0, 1]: total excess cell area above target_density
      // across all bins, divided by total movable cell area (fillers excluded).
      // Equivalent to Xplace's overflow metric; convergence target is typically ~0.07.
      float computeTotalOverflow(float target_density, float total_movable_area);

    private:
      // Grid data members
      Box die_area_;
      int bins_per_row_;
      int bins_per_col_;
      float bin_width_;
      float bin_height_;

      // 2D grid of bins to compute eField
      std::unique_ptr<Bin[]> density_bins_;


      int binIndexX(const float x) const;
      int binIndexY(const float y) const;
      int binIndex(const int ix, const int iy) const;

  };

}
#endif
