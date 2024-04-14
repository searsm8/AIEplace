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
    double getXsize() { return m_macro_class->getXsize(); }
    double getYsize() { return m_macro_class->getYsize(); }
    double getArea() { return m_macro_class->getArea(); }

    // Setters
    void setMacroClass(MacroClass* macro) { m_macro_class = macro; }
};

AIEPLACE_NAMESPACE_END

#endif