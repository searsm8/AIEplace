#ifndef AIEPLACE_DATABASE_H
#define AIEPLACE_DATABASE_H

#include <sstream>

#include "Common.h"
#include "Component.h"
#include "Library.h"
//#include "Pin.h"
//#include "Node.h"
//#include "Net.h"
//#include "Bin.h"
//#include "Logger.h"

namespace AIEPLACE_NAMESPACE {

  class DataBase
  {
    public:
      DataBase() {}
      DataBase(fs::path input_dir) : design_dir(input_dir) {};
      virtual ~DataBase() {}

      ComponentTypeLibrary  typeLib;
      ComponentLibrary      componentLib;

    private:
      // Path to find directory containing design data.
      // Expects to find a cells.lef and floorplan.def file
      fs::path design_dir;

  };

}

#endif
