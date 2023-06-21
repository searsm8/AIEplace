
// TODO: add header
#ifndef AIEPLACE_POSITION_H
#define AIEPLACE_POSITION_H

#include "AIEplace.h"

AIEPLACE_NAMESPACE_BEGIN

class Position
{
private:
    // Data members
    position_type m_x;
    position_type m_y;

public:
    // Constructors
    Position() : m_x(0), m_y(0) {}

    Position(position_type initial_x, position_type  initial_y) : m_x(initial_x), m_y(initial_y) {}

    // Member Functions
    // Getters
    position_type getX() { return m_x; }
    position_type getY() { return m_y; }

    // Setters
    void setX(position_type new_x) { m_x = new_x; }
    void setY(position_type new_y) { m_y = new_y; }

    void setPosition(position_type new_x, position_type new_y) { m_x = new_x; m_y = new_y;}
    void setPosition(Position pos) { m_x = pos.getX(); m_y = pos.getY(); }

    string to_string();

    void translate(position_type move_x, position_type move_y);

    void translate(Position pos);

}; // End of class Position



AIEPLACE_NAMESPACE_END

#endif