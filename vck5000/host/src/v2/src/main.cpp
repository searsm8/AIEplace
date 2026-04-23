//#include "AIEplace.h"
#include "DataBase.h"
#include "Library.h"

#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>

namespace AIEPLACE_NAMESPACE {

  // ---- Enums pretty-print ----
  std::ostream& operator<<(std::ostream& os, ComponentState s) {
    switch (s) {
      case ComponentState::Placed:   return os << "Placed";
      case ComponentState::Unplaced: return os << "Unplaced";
    }
    return os << "UnknownState(" << static_cast<int>(s) << ')';
  }

  std::ostream& operator<<(std::ostream& os, ComponentKind k) {
    switch (k) {
      case ComponentKind::Unknown:   return os << "Unknown";
      case ComponentKind::LogicCell: return os << "LogicCell";
      case ComponentKind::Macro:     return os << "Macro";
      case ComponentKind::Filler:    return os << "Filler";
      case ComponentKind::IOPad:     return os << "IOPad";
    }
    return os << "UnknownKind(" << static_cast<int>(k) << ')';
  }

  // ---- Dump the vector and map ----
  void print_component_type_library(const ComponentTypeLibrary& lib, std::ostream& os = std::cout)
  {
    os << "component_library (size=" << lib.size() << "):\n";
    const auto& map = lib.index_map();
    for (const auto& [name, idx] : map) {
      os << "[" << idx << "]: ";
      const ComponentType& ct = lib[idx];
      printComponentType(os, name, ct);
      os << "\n";
    }
  }
}

int main(int argc, char *argv[])
{

    //Logger::setup_logging();

    //std::string config_filepath = (argc > 1) ? argv[1] : "host/run_config.json"; // default

    // Instantiate the placer
    //AIEplace::Placer placer(config_filepath);
    AIEplace::DataBase db("host/benchmarks/ispd2015/mgc_des_perf_a");
    AIEplace::print_component_type_library(db.typeLib);

    // Print DataBase info
    //placer.db.printInfo(); 
    //placer.db.computeTotalComponentArea();
    //placer.db.printNodes();
    //placer.db.printNets();
    //placer.db.printNetsByDegree();

    // Run the placer 
    //placer.run();

    //placer.plotHistories();
    //placer.printFinalResults();

    return 0;
}
