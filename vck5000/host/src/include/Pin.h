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

public:
    // Constructors
    using Node::Node;

    // Member Functions
    // Getters
    float getXsize() { return m_bounding_box.getXsize(); }
    float getYsize() { return m_bounding_box.getYsize(); }
    float getArea()  { return m_bounding_box.getArea();  }

    // Setters
    void setBoundingBox(position_type xl, position_type xh, position_type yl, position_type yh)
    { m_bounding_box = Box<position_type>(Position(xl, xh), Position(yl, yh)); }

};


AIEPLACE_NAMESPACE_END

#endif