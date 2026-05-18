
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
  std::ostream& printComponentType(std::ostream& os, const std::string& name, const ComponentType& ct) {
    os << "ComponentType{\n"
      << "  name: "      << name << "\n"
      << "  kind: "      << ct.kind << "\n"
      << "  width: "     << ct.width << "\n"
      << "  height: "    << ct.height << "\n"
      << "  numPins: "   << ct.numPins() << "\n";

    if (ct.numPins() > 0) {
      os << "  pins:\n";
      for (uint32_t i = 0; i < ct.numPins(); ++i) {
        os << "    [" << i << "]: (" << ct.pinDx[i] << ", " << ct.pinDy[i] << ")\n";
      }
    } else {
      os << "  pins: <none or not initialized>\n";
    }

    os << "}";
    return os;
  }

  // ---- Component pretty-print ----
  std::ostream& printComponent(std::ostream& os, const std::string& name, const Component& c) {
    os << "Component{\n"
      << "  name:              "  << name << "\n"
      << "  ComponentType idx: "  << c.libraryId << "\n"
      << "  origin:            (" << c.origin.x << ", " << c.origin.y << ")\n"
      << "  state:             "  << c.state << "\n";

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
    const auto& array = lib.data();
    const auto n = lib.size();
    os << "component_type_library (size=" << n << "):\n";
    for (uint32_t idx = 0; idx < n; idx++) {
      os << "[" << idx << "]: ";
      if(lib.is_locked()) {
        const ComponentType& ct = lib[idx];
        printComponentType(os, lib.name_at(idx), ct);
      } else {
        const ComponentType& ct = lib.at_index(idx);
        printComponentType(os, lib.name_at(idx), ct);
      }
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
    const auto& array = lib.data();
    const auto n = lib.size();
    os << "component_library (size=" << n << "):\n";
    for (uint32_t idx = 0; idx < n; idx++) {
      os << "[" << idx << "]: ";
      if(lib.is_locked()) {
        const Component& c = lib[idx];
        printComponent(os, lib.name_at(idx), c);
      } else {
        const Component& c = lib.at_index(idx);
        printComponent(os, lib.name_at(idx), c);
      }
      os << "\n";

      //if (idx > 10)
      //  break;
    }
  }
}
