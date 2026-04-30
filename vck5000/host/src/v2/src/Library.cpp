
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

  // ---- Dump the vector and map ----
  void print_component_type_library(const ComponentTypeLibrary& lib, std::ostream& os)
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
