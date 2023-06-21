#include "Position.h"

AIEPLACE_NAMESPACE_BEGIN

string Position::to_string()
{
    return "@(" + std::to_string(m_x) + ", " + std::to_string(m_y) + ")";
}

void Position::translate(position_type move_x, position_type move_y) 
{
    m_x += move_x;
    m_y += move_y;
}

void Position::translate(Position pos) 
{
    m_x += pos.getX();
    m_y += pos.getY();
}

AIEPLACE_NAMESPACE_END