// TODO: add header
#ifndef AIEPLACE_PIN_H
#define AIEPLACE_PIN_H

#include "Common.h"
#include "Node.h"
#include "Bin.h"

AIEPLACE_NAMESPACE_BEGIN

class Pin : public Node
{
private:
    Box<position_type> m_bounding_box;
    string m_direction;
    string m_layer;

public:
    // Constructors
    using Node::Node;

    // Member Functions
    // Getters
    float getXsize() { return m_bounding_box.getXsize(); }
    float getYsize() { return m_bounding_box.getYsize(); }
    float getArea()  { return m_bounding_box.getArea();  }
    string getDirection() { return m_direction; }
    string getLayer() { return m_layer; }
    Box<position_type> & getBoundingBox() { return m_bounding_box; }

    // Setters
    void setDirection(string dir) { m_direction = dir; }
    void setBoundingBox(position_type llx, position_type lly, position_type urx, position_type ury)
    { m_bounding_box = Box<position_type>(Position(llx, lly), Position(urx, ury)); }

};


AIEPLACE_NAMESPACE_END

#endif