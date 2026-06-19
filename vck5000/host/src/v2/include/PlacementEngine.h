#ifndef PLACEMENTENGINE_H
#define PLACEMENTENGINE_H

#include "FPGADriver.h"

namespace AIEPLACE_NAMESPACE {

  class IPlacementEngine {
    public:
      virtual ~IPlacementEngine() = default;

      virtual void set_data() = 0;
      virtual void run() = 0;

  };

  class VCK5000PlacementEngine : public IPlacementEngine
  {
    public:
      VCK5000PlacementEngine(DataBase& db, std::unique_ptr<IFPGADriver> fpga_driver) : db_(db), fpga_(std::move(fpga_driver)) {};
      ~VCK5000PlacementEngine() override = default;

      void set_data() override;
      void run() override;

    private:
      std::unique_ptr<IFPGADriver> fpga_;
      DataBase& db_;

  };

  class CPUPlacementEngine : public IPlacementEngine
  {
    public:
      CPUPlacementEngine(DataBase& db) : db_(db) {};
      ~CPUPlacementEngine() override = default;

      void set_data() override;
      void run() override;

    private:
      void partials_simple();
      DataBase& db_;

  };

}

#endif
