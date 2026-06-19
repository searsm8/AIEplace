#ifndef AIEPLACE_TYPES_H
#define AIEPLACE_TYPES_H

#include "Common.h"

namespace AIEPLACE_NAMESPACE {

  template <typename T>
  using matrix = std::vector<std::vector<T>>;

  template <typename T, std::size_t N>
  using FixedSquareMatrix = std::array<std::array<T, N>, N>;

  // Contains XY data, which might be coordinates or any other pair of data.
  struct XY
  {
    float x;
    float y;
    void clear() { x = 0.0f; y = 0.0f;}

    // default constructor initializes to 0,0
    XY () { clear(); }
    XY (float x_val, float y_val) : x(x_val), y(y_val) {}

    XY operator+(const XY& other) const {
      return XY{x + other.x, y + other.y};
    }

    XY& operator+=(const XY& other) {
      x += other.x;
      y += other.y;
      return *this;
    }

    XY operator-(const XY& other) const {
      return XY{x - other.x, y - other.y};
    }

    XY operator-=(const XY& other) {
      x -= other.x;
      y -= other.y;
      return *this;
    }

    XY operator*(float s) const {
      return XY{x * s, y * s};
    }

    // friend: defines a free function so float*XY works (member operator only handles XY*float)
    friend XY operator*(float s, const XY& v) {
      return XY{s * v.x, s * v.y};
    }

    void setXY(float new_x, float new_y) { x = new_x; y = new_y; }
    void setXY(XY other) { x = other.x; y = other.y; }

    void translate(float dx, float dy) { x += dx; y += dy; }
    void translate(XY move) { x += move.x; y += move.y; }

    std::string to_string() {
      std::ostringstream ss;
      ss << std::setprecision(2) << std::fixed;
      ss << "@(" << x << ", " << y << ")";
      return ss.str();
    }

  };

  typedef XY Coordinate;    // alias for XY
  typedef XY Position;      // alias for XY
  typedef XY Gradient;      // alias for XY
  typedef XY Delta;         // alias for XY

  class Box
  {
    private:
      Position m_ll; // Lower left corner of Box
      Position m_ur; // Upper right corner of Box

    public:
      // Constructors
      Box() {}
      Box(Position a, Position b) : m_ll(a), m_ur(b) {}
      Box(float width, float height): m_ll(Position(0.0f, 0.0f)), m_ur(Position(width, height)) {}
      Box(float xl, float yl, float xh, float yh) : m_ll(Position(xl, yl)), m_ur(Position(xh, yh)) {}

      // Member Functions
      // Getters
      Position getPos() { return m_ll; }
      Position getPosBottomLeft() const { return m_ll; }
      Position getPosTopRight() const { return m_ur; }
      float getXsize() const { return abs(m_ll.x - m_ur.x); }
      float getYsize() const { return abs(m_ll.y - m_ur.y); }
      float getArea() const  { return getXsize() * getYsize(); }

      std::string to_string() {
        std::stringstream s;
        s << std::setprecision(2) << std::fixed;
        s << "Box@(" << m_ll.to_string() << ", " << m_ur.to_string() << ")";
        return s.str();
      }

      std::string getDEFstring() {
        std::stringstream s;
        s << " ( " << m_ll.x << " " << m_ll.y << " ) ( "
          << m_ur.x << " " << m_ur.y << " ) ";
        return s.str();
      }

      Box operator+(const XY& vec) const {
        return Box{m_ll + vec, m_ur + vec };
      }

      float overlap(const Box& b) const {
        float x_overlap = std::max(0.0f, std::min(m_ur.x, b.getPosTopRight().x) - std::max(m_ll.x, b.getPosBottomLeft().x));
        float y_overlap = std::max(0.0f, std::min(m_ur.y, b.getPosTopRight().y) - std::max(m_ll.y, b.getPosBottomLeft().y));
        return x_overlap * y_overlap;
      }

      // Setters
      void setDimensions(float ll_x, float ll_y, float ur_x, float ur_y) {
        m_ll.x = ll_x;
        m_ll.y = ll_y;
        m_ur.x = ur_x;
        m_ur.y = ur_y;
      }
      void setDimensions(float width, float height) {
        m_ll.x = 0.0f;
        m_ll.y = 0.0f;
        m_ur.x = width;
        m_ur.y = height;
      }

  };

  // a Bin is a (relatively) small portion of the die area
  // This struct collects useful data within the area of the Bounding Box
  struct Bin
  {
    Box box;

    // Total Node overlap within this bin
    float overlapArea;
    float getDensity() {
      return overlapArea/box.getArea();
    }

    // Local weight parameter for density in this bin.
    // Bigger local_density_weight means the eField will push harder in this bin.
    float local_density_weight;

  };

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
    Component() : name(""), typeIndex(0), origin(0.0f, 0.0f), state(ComponentState::Unplaced) {}
    Component(std::string n, uint32_t typeId, float x = 0.0f, float y = 0.0f,
        ComponentState s = ComponentState::Unplaced)
      : name(n), typeIndex(typeId), origin(x, y), state(s) {}

    std::string name;
    uint32_t typeIndex;

    Coordinate origin;
    ComponentState state;
  };


  struct ComponentType {

    ComponentType() : name(""), kind(ComponentKind::Unknown), box(Box{0.0f, 0.0f}) {}
    ComponentType(std::string n, ComponentKind k, float w, float h)
      : name(n), kind(k), box(Box{w,h}) {}

    // identifying info
    std::string name;
    ComponentKind kind;

    // geometry
    Box box;

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
      return static_cast<uint32_t>(a.typeIndex) < static_cast<uint32_t>(b.typeIndex);
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
