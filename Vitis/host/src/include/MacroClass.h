// TODO: add header
#ifndef AIEPLACE_MACROCLASS_H
#define AIEPLACE_MACROCLASS_H

#include "Common.h"

AIEPLACE_NAMESPACE_BEGIN

class MacroClass
{
private:
    // Data members
    string m_name;
    float m_Xsize;
    float m_Ysize;

public:
    // Constructors
    MacroClass() {}
    MacroClass(string name) : m_name(name), m_Xsize(0), m_Ysize(0) {}

    MacroClass(string name, float Xsize, float Ysize) : 
                    m_name(name), m_Xsize(Xsize), m_Ysize(Ysize) {}

    // Member Functions
    // Getters
    string getName() { return m_name; }
    float getXsize() { return m_Xsize; }
    float getYsize() { return m_Ysize; }
    float getArea()  { return m_Xsize*m_Ysize; }


    // Setters
    void setSize(float Xsize, float Ysize) { m_Xsize = Xsize; m_Ysize = Ysize; }

}; // End of class Node


AIEPLACE_NAMESPACE_END

#endif