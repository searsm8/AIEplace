#ifndef AIEPLACE_DATABASE_H
#define AIEPLACE_DATABASE_H

#include <sstream>

#include "Common.h"
#include "Types.h"
#include "Library.h"
#include "Parsers.h"
#include "Grid.h"

namespace AIEPLACE_NAMESPACE {

  class DataBase
  {
    public:
      DataBase(fs::path design_dir) :
        bins_per_row_(1024), bins_per_col_(1024)
      { init(design_dir); }
      DataBase(fs::path design_dir, uint16_t bins_per_row_and_col) :
        bins_per_row_(bins_per_row_and_col), bins_per_col_(bins_per_row_and_col)
      { init(design_dir); }
      DataBase(fs::path design_dir, uint16_t bins_per_row, uint16_t bins_per_col) :
        bins_per_row_(bins_per_row), bins_per_col_(bins_per_col)
      { init(design_dir); }

      virtual ~DataBase() {}

      ComponentTypeLibrary      typeLib_;
      ComponentLibrary          componentLib_;
      std::map<uint16_t, NetLibrary>   netLib_;
      Grid die_;

      void calculateBinDensity();

      void addGradient(const Net::NetPin p, Gradient g);
      void resetGradients();

      Coordinate getNetPinCoordinates(Net::NetPin p) const;
      void print_gradients(std::ostream& os = std::cout) const {
        for (auto& g : gradients_) {
          os << "(" << g.x << "," << g.y << ") ";
        }
        os << "\n";
      }

      void printInfo() const;

    private:
      void init(fs::path design_dir);
      // Path to find directory containing design data.
      // Expects to find a cells.lef and floorplan.def file
      void parseDesign(fs::path design_dir);
      void initializeComponentCoordinates();
      void addFillerType();
      void addFillers(float target_density);

      // Contains the movement delta per iteration
      // for component at component_idx
      std::vector<Gradient> gradients_;

      //Density Bins
      uint16_t bins_per_row_;
      uint16_t bins_per_col_;

  };

}

#endif
