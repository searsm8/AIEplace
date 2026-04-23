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

  class Component
  {
    public:
      Component(const char* n, uint32_t li, float x, float y,
          ComponentState s = ComponentState::Unplaced)
        : instName(n), libraryId(li), origin(x, y), state(s) {}

    private:
      // TODO: name is only for debug, could be removed if
      // component name to index map works well
      std::string instName;
      uint32_t    libraryId;

      Coordinate origin;
      ComponentState state;
  };


  struct ComponentType {

    ComponentType(ComponentKind k, float w, float h,
        uint32_t pins = 0, const int32_t* dx = nullptr, const int32_t* dy = nullptr)
      : kind(k), width(w), height(h),
      numPins(pins), pinDx(dx), pinDy(dy) {}

    // identifying info

    ComponentKind kind;
    uint32_t numPins;

    // geometry
    float width;
    float height;
    float getArea() { return width * height; };
    // any other static properties: drive strength, delay, etc.

    // pin info
    // offsets from cell origin
    const int32_t *pinDx;   // size = numPins
    const int32_t *pinDy;   // size = numPins
  };


}

#endif
