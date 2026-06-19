
#include "Library.h"

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

  // ---- ComponentType pretty-print ----
  std::ostream& printComponentType(std::ostream& os, const ComponentType& ct) {
    os << "ComponentType{\n"
      << "  name: "      << ct.name << "\n"
      << "  kind: "      << ct.kind << "\n"
      << "  width: "     << ct.box.getXsize() << "\n"
      << "  height: "    << ct.box.getYsize() << "\n"
      << "  numPins: "   << ct.numPins() << "\n";

    if (ct.numPins() > 0) {
      os << "  pins:\n";
      for (uint32_t i = 0; i < ct.numPins(); ++i) {
        os << "    " << ct.pins[i].name << " [" << i << "]: (" << ct.pins[i].Dx << ", " << ct.pins[i].Dy << ")\n";
      }
    } else {
      os << "  pins: <none or not initialized>\n";
    }

    os << "}";
    return os;
  }

  // ---- Component pretty-print ----
  std::ostream& printComponent(std::ostream& os, const Component& c) {
    os << "Component{\n"
      << "  name:              "  << c.name << "\n"
      << "  ComponentType idx: "  << c.typeIndex << "\n"
      << "  origin:            (" << c.origin.x << ", " << c.origin.y << ")\n"
      << "  state:             "  << c.state << "\n";

    os << "}";
    return os;
  }

  std::ostream& printNet(std::ostream& os, const Net& n) {
    os << "Net{\n"
       << "  name:              "  << n.name << "\n";
    if (! n.netpins.empty() ) {
      os << "   ";
      for (const auto& np : n.netpins ) {
        os << " ( " << np.component_idx << ", " << np.pin_idx << " )";
      }
      os << "\n";
    } else {
      os << " <no pins on net>\n";
    }
    os << "}";
    return os;
  }

  // ---- Dump the vector and map ----
  void print_component_type_library(const ComponentTypeLibrary& lib, std::ostream& os)
  {
    if(!lib.is_locked()) {
      os << "#############################\n";
      os << "# LIBRARY IS NOT LOCKED!    #\n";
      os << "# Indeces can still change! #\n";
      os << "#############################\n";
      os << "\n";
    }
    os << "component_type_library (size=" << lib.size() << "):\n";
    std::size_t idx = 0;
    for (const auto& ct : lib) {
      os << "[" << idx++ << "]: ";
      printComponentType(os, ct);
      os << "\n";
    }
  }
  void print_component_library(const ComponentLibrary& lib, std::ostream& os)
  {
    if(!lib.is_locked()) {
      os << "#############################\n";
      os << "# LIBRARY IS NOT LOCKED!    #\n";
      os << "# Indeces can still change! #\n";
      os << "#############################\n";
      os << "\n";
    }
    os << "component_library (size=" << lib.size() << "):\n";
    std::size_t idx = 0;
    for (const auto& c : lib) {
      os << "[" << idx++ << "]: ";
      printComponent(os, c);
      os << "\n";
    }
  }
  void print_net_library(const std::map<uint16_t, NetLibrary>& lib, std::ostream& os)
  {
    os << "net_library (netsizes=[ ";
    uint32_t total_nets = 0;
    for (const auto& [netsize, nets] : lib) {
      os << netsize << "{" << nets.size() << "} ";
      total_nets += nets.size();
    }
    os << " ], total num of nets=" << total_nets << ")\n";
    //if(!lib.is_locked()) {
    //  os << "#############################\n";
    //  os << "# LIBRARY IS NOT LOCKED!    #\n";
    //  os << "# Indeces can still change! #\n";
    //  os << "#############################\n";
    //  os << "\n";
    //}
    //std::size_t idx = 0;
    //for (const auto& n : lib) {
    //  os << "[" << idx++ << "]: ";
    //  printNet(os, n);
    //  os << "\n";
    //}
  }
}
