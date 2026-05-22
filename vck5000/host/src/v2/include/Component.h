// TODO: add header
#ifndef AIEPLACE_COMPONENT_H
#define AIEPLACE_COMPONENT_H

#include "Common.h"
#include "Node.h"

namespace AIEPLACE_NAMESPACE {

  enum class ComponentState : uint8_t {
    Placed = 0,
    Unplaced = 1
  };
  enum class ComponentKind : uint8_t {
    Unknown = 0,
    LogicCell = 1,
    Macro = 2,
    Filler = 3,
    IOPad = 4
  };

  struct Net
  {
    Net() : name("") {}
    Net(std::string n) : name(n) {}

    struct NetPin {
      uint32_t component_idx;
      uint32_t pin_idx;
    };
    std::string name;
    std::vector<NetPin> netpins;
  };

  struct Component
  {
    Component() : name(""), libraryId(0), origin(0.0f, 0.0f), state(ComponentState::Unplaced) {}
    Component(std::string n, uint32_t li, float x, float y,
        ComponentState s = ComponentState::Unplaced)
      : name(n), libraryId(li), origin(x, y), state(s) {}

    std::string name;
    uint32_t libraryId;

    Coordinate origin;
    ComponentState state;
  };


  struct ComponentType {

    ComponentType() : name(""), kind(ComponentKind::Unknown), width(0.0f), height(0.0f) {}
    ComponentType(std::string n, ComponentKind k, float w, float h)
      : name(n), kind(k), width(w), height(h) {}

    // identifying info
    std::string name;
    ComponentKind kind;

    // geometry
    float width;
    float height;
    float getArea() { return width * height; };
    // any other static properties: drive strength, delay, etc.

    // pin info
    // offsets from cell origin (left bottom corner)
    struct ComponentPin {
      ComponentPin(std::string n, float x, float y) : name(n), Dx(x), Dy(y) {}

      std::string name;
      float Dx;
      float Dy;
    };
    std::vector<ComponentPin> pins;
    std::unordered_map<std::string, uint32_t> pinIndex;
    uint32_t numPins() const { return static_cast<uint32_t>(pins.size()); }
  };

  struct ComponentTypeComparator {
    bool operator()(const ComponentType& a, const ComponentType& b) const
    {
      return static_cast<uint8_t>(a.kind) < static_cast<uint8_t>(b.kind);
    }
  };

  struct ComponentComparator {
    bool operator()(const Component& a, const Component& b) const
    {
      return static_cast<uint32_t>(a.libraryId) < static_cast<uint32_t>(b.libraryId);
    }
  };

  struct NetComparator {
    bool operator()(const Net& a, const Net& b) const
    {
      return static_cast<uint32_t>(a.netpins.size()) < static_cast<uint32_t>(b.netpins.size());
    }
  };

}

#endif
