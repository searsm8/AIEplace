// TODO: add header
#ifndef AIEPLACE_NODE_H
#define AIEPLACE_NODE_H

#include "AIEplace.h"
#include "Position.h"
#include "MacroClass.h"

AIEPLACE_NAMESPACE_BEGIN

class Net;

enum PlacementStatus
{
    UNPLACED = 0x0, 
    PLACED = 0x1, 
    FIXED = 0x2, 
    DUMMY_FIXED = 0x3, ///< initially not fixed, but fixed by the placer 
    UNKNOWN = 0x4 
};

class Node
{
private:
    // Data members
    string m_name;
    MacroClass* m_macro;
    Position m_pos;
    PlacementStatus m_status;
    std::vector<Net*> mv_nets; // List of all nets this node is on

public:
    // Constructors
    Node() : m_name("") {}
    Node(string name) : m_name(name), m_pos(0, 0) {}

    // Member Functions
    // Getters
    string name() { return m_name; }
    Position getPosition() { return m_pos; }
    PlacementStatus status() { return m_status; }
    MacroClass* macro() { return m_macro; }

    std::vector<Net*> getNets() { return mv_nets; }

    // Setters
    void setPosition(Position pos) { m_pos = pos; }
    void setPlacementStatus(string status) 
    { 
        if(status == "UNPLACED")
            m_status = UNPLACED;
        else if(status == "PLACED")
            m_status = PLACED;
        else if(status == "FIXED")
            m_status = FIXED;
        else if(status == "DUMMY_FIXED")
            m_status = DUMMY_FIXED;
        else
            m_status = UNKNOWN;
    }

    void setMacroClass(MacroClass* macro) { m_macro = macro; }

}; // End of class Node


AIEPLACE_NAMESPACE_END

#endif