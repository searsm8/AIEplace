
// TODO: add header
#ifndef AIEPLACE_POSITION_H
#define AIEPLACE_POSITION_H

#include "Common.h"
#include <sstream>

AIEPLACE_NAMESPACE_BEGIN

template<typename T>
class Position
{
public:
    // Data members
    T x;
    T y;

    // Constructors
    Position() : x(0), y(0) {}

    Position(T initial_x, T  initial_y) : x(initial_x), y(initial_y) {}

    // Member Functions
    // Getters
    const T& getX() { return x; }
    const T& getY() { return y; }

    // Setters
    void setX(T new_x) { x = new_x; }
    void setY(T new_y) { y = new_y; }

    void setPosition(T new_x, T new_y) { x = new_x; y = new_y;}
    void setPosition(Position pos) { x = pos.getX(); y = pos.getY(); }

    string to_string() {
        std::stringstream s;
        s << std::setprecision(2) << std::fixed;
        s << "@(" << x << ", " << y << ")";
        return s.str();
    }

    void translate(T move_x, T move_y)
    {
        x += move_x;
        y += move_y;
    }

    void translate(XY move)
    {
        x += move.x;
        y += move.y;
    }

    // Operators
    Position<T> operator+(Position<T> other)
    {
        Position<T> pos;
        pos.x = x + other.x;
        pos.y = y + other.y;
        return pos;
    }

    Position<T> operator-(Position<T> other)
    {
        Position<T> pos;
        pos.x = x - other.x;
        pos.y = y - other.y;
        return pos;
    }


}; // End of class Position

AIEPLACE_NAMESPACE_END

#endif