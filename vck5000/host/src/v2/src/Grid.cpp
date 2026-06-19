#include "Grid.h"
#include "Logger.h"

namespace AIEPLACE_NAMESPACE {

  void Grid::init()
  {
    density_bins_ = std::make_unique<Bin[]>(bins_per_row_ * bins_per_col_);

    bin_width_  = die_area_.getXsize() / (float)bins_per_row_;
    bin_height_ = die_area_.getYsize() / (float)bins_per_col_;

    for (int iy = 0; iy < bins_per_col_; ++iy) {
      for (int ix = 0; ix < bins_per_row_; ++ix) {
        float ll_x = ix * bin_width_;
        float ll_y = iy * bin_height_;
        float ur_x = ll_x + bin_width_;
        float ur_y = ll_y + bin_height_;

        Bin& bin = density_bins_[binIndex(ix, iy)];
        bin.box = Box{ll_x, ll_y, ur_x, ur_y};
        bin.overlapArea = 0.0f;
      }
    }
  }
  void Grid::setDimensions(float width, float height, int bins_per_row, int bins_per_col) {
    die_area_ = Box{width, height};
    bins_per_row_ = bins_per_row;
    bins_per_col_ = bins_per_col;
    init();
  }

  /* @brief: Reset all nodes and nets for the next iteration.
  */
  void Grid::clearDensities()
  {
    for(uint32_t i = 0; i < bins_per_row_*bins_per_col_; i++)
      density_bins_[i].overlapArea = 0.0f;
  }

  float Grid::computeTotalOverflow(float target_density, float total_movable_area)
  {
    float overflow_area = 0;
    for (int col = 0; col < bins_per_row_; col++)
      for (int row = 0; row < bins_per_col_; row++) {
        float bin_capacity = density_bins_[binIndex(col, row)].box.getArea() * target_density;
        float excess = density_bins_[binIndex(col, row)].overlapArea - bin_capacity;
        overflow_area += std::max(0.0f, excess);
      }
    return overflow_area / (total_movable_area + 1e-8f);
  }

  int Grid::binIndexX(const float x) const {
    int ix = static_cast<int>(x / bin_width_);
    if (ix < 0) ix = 0;
    if (ix >= bins_per_row_) ix = bins_per_row_ - 1;
    return ix;
  }
  int Grid::binIndexY(const float y) const {
    int iy = static_cast<int>(y / bin_height_);
    if (iy < 0) iy = 0;
    if (iy >= bins_per_col_) iy = bins_per_col_ - 1;
    return iy;
  }
  // Convert 2D (ix,iy) to 1D index into bins vector
  int Grid::binIndex(const int ix, const int iy) const {
    return iy * bins_per_row_ + ix;
  }

  void Grid::addComponentAreaToDensityBins(const Box& compArea)
  {
    // Determine which bin indices this component overlaps
    int ixMin = binIndexX(compArea.getPosBottomLeft().x);
    int ixMax = binIndexX(compArea.getPosTopRight().x);
    int iyMin = binIndexY(compArea.getPosBottomLeft().y);
    int iyMax = binIndexY(compArea.getPosTopRight().y);

    // Add area to each corresponding bin accumulator
    for (int iy = iyMin; iy <= iyMax; ++iy) {
      for (int ix = ixMin; ix <= ixMax; ++ix) {
        Bin& bin = density_bins_[binIndex(ix, iy)];
        float overlapArea = bin.box.overlap(compArea);
        if (overlapArea > 0.0f) {
          bin.overlapArea += overlapArea;
        }
      }
    }
  }

}
