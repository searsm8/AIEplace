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
    Net(const std::string n) : name_(n) {}

    struct NetPinIndex {
      uint32_t component;
      uint32_t type;
    };

    std::vector<NetPinIndex> pin_indeces;
    //TODO: remove name after map works
    const std::string name_;
  };

  struct Component
  {
    Component() : libraryId(0), origin(0.0f, 0.0f), state(ComponentState::Unplaced) {}
    Component(uint32_t li, float x, float y,
        ComponentState s = ComponentState::Unplaced)
      : libraryId(li), origin(x, y), state(s) {}

    uint32_t libraryId;

    Coordinate origin;
    ComponentState state;
  };


  struct ComponentType {

    ComponentType() : kind(ComponentKind::Unknown), width(0.0f), height(0.0f) {}
    ComponentType(ComponentKind k, float w, float h)
      : kind(k), width(w), height(h) {}

    // identifying info

    ComponentKind kind;

    // geometry
    float width;
    float height;
    float getArea() { return width * height; };
    // any other static properties: drive strength, delay, etc.

    // pin info
    // offsets from cell origin
    std::vector<float> pinDx;
    std::vector<float> pinDy;
    std::unordered_map<std::string, uint32_t> pinIndex;
    uint32_t numPins() const { return static_cast<uint32_t>(pinDx.size()); }
  };

  struct ComponentTypeComparator {
    bool operator()(const std::pair<std::string, ComponentType>& a,
        const std::pair<std::string, ComponentType>& b) const
    {
      return static_cast<uint8_t>(a.second.kind) < static_cast<uint8_t>(b.second.kind);
    }
  };

  struct ComponentComparator {
    bool operator()(const std::pair<std::string, Component>& a,
        const std::pair<std::string, Component>& b) const
    {
      return static_cast<uint32_t>(a.second.libraryId) < static_cast<uint32_t>(b.second.libraryId);
    }
  };

}

#endif
