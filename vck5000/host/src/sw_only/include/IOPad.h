// IOPad.h — Top-level design IO pads (primary inputs/outputs).
// These are the "Pins" in the DEF PINS section — fixed ports on the chip boundary.
// Not to be confused with NetPin (a connection point on a component within a net).
#ifndef AIEPLACE_IOPAD_H
#define AIEPLACE_IOPAD_H

#include "Common.h"
#include "Node.h"
#include "Bin.h"

AIEPLACE_NAMESPACE_BEGIN

class IOPad : public Node
{
private:
    Box m_bounding_box;
    string m_direction;
    string m_layer;

public:
    using Node::Node;

    // Getters
    float getXsize() { return m_bounding_box.getXsize(); }
    float getYsize() { return m_bounding_box.getYsize(); }
    float getArea()  { return m_bounding_box.getArea();  }
    string getDirection() { return m_direction; }
    string getLayer() { return m_layer; }
    Box & getBoundingBox() { return m_bounding_box; }

    // Setters
    void setDirection(string dir) { m_direction = dir; }
    void setBoundingBox(position_type llx, position_type lly, position_type urx, position_type ury)
    { m_bounding_box = Box(Position(llx, lly), Position(urx, ury)); }

};


AIEPLACE_NAMESPACE_END

#endif
