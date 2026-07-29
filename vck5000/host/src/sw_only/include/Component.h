/**
 * @file Component.h
 * @brief A placed instance of a macro — a Node subclass that gets its size from its MacroClass.
 */
#pragma once

#include "Common.h"
#include "Node.h"

AIEPLACE_NAMESPACE_BEGIN

class MacroClass;

// Component class represents an instance of a macro in the design. 
// It inherits from Node, which contains all the information about 
// the position, nets, and overlaps of this component. 
// The Component class adds a pointer to its MacroClass, 
// which contains information about the size of this component.
class Component : public Node
{
private:
    MacroClass* m_macro_class;

public:
    // Constructors
    using Node::Node;

    // Member Functions
    // Getters
    MacroClass* getMacro() { return m_macro_class; }
    float getXsize() { return m_macro_class->getXsize(); }
    float getYsize() { return m_macro_class->getYsize(); }
    float getArea() { return m_macro_class->getArea(); }

    // Setters
    void setMacroClass(MacroClass* macro) { m_macro_class = macro; }
};

AIEPLACE_NAMESPACE_END

