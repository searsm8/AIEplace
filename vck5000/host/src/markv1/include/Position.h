
// TODO: add header
#ifndef AIEPLACE_POSITION_H
#define AIEPLACE_POSITION_H

#include "Common.h"
#include <sstream>

AIEPLACE_NAMESPACE_BEGIN

template<typename T>
class Position
{
private:
    // Data members
    T m_x;
    T m_y;

public:
    // Constructors
    Position() : m_x(0), m_y(0) {}

    Position(T initial_x, T  initial_y) : m_x(initial_x), m_y(initial_y) {}

    // Member Functions
    // Getters
    const T& getX() { return m_x; }
    const T& getY() { return m_y; }

    // Setters
    void setX(T new_x) { m_x = new_x; }
    void setY(T new_y) { m_y = new_y; }

    void setPosition(T new_x, T new_y) { m_x = new_x; m_y = new_y;}
    void setPosition(Position pos) { m_x = pos.getX(); m_y = pos.getY(); }

    string to_string() {
        std::stringstream s;
        s << std::setprecision(2) << std::fixed;
        s << "@(" << m_x << ", " << m_y << ")";
        return s.str();
    }

    void translate(T move_x, T move_y)
    {
        m_x += move_x;
        m_y += move_y;
    }

    void translate(Position pos)
    {
        m_x += pos.getX();
        m_y += pos.getY();
    }

    // Operators
    Position<T> operator+(Position<T> other)
    {
        Position<T> pos;
        pos.m_x = m_x + other.m_x;
        pos.m_y = m_y + other.m_y;
        return pos;
    }


}; // End of class Position

AIEPLACE_NAMESPACE_END

#endif