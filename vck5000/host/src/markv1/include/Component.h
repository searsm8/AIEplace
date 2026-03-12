// TODO: add header
#ifndef AIEPLACE_COMPONENT_H
#define AIEPLACE_COMPONENT_H

#include "Common.h"
#include "Node.h"

AIEPLACE_NAMESPACE_BEGIN

class MacroClass;

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

#endif