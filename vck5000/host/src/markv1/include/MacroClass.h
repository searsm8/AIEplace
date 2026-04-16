// TODO: add header
#ifndef AIEPLACE_MACROCLASS_H
#define AIEPLACE_MACROCLASS_H

#include "Common.h"

AIEPLACE_NAMESPACE_BEGIN

class MacroClass
{
private:
    string m_name;
    float m_Xsize;
    float m_Ysize;
    float m_area;

    // Per-pin offsets relative to macro origin, keyed by pin name.
    // Extracted from LEF RECT geometry (center of first RECT per pin).
    std::map<string, Position> m_pin_offsets;

public:
    MacroClass() {}
    MacroClass(string name) : m_name(name), m_Xsize(0), m_Ysize(0) { m_area = 0; }

    MacroClass(string name, float Xsize, float Ysize) :
                    m_name(name), m_Xsize(Xsize), m_Ysize(Ysize) { m_area = Xsize * Ysize; }

    // Getters
    string getName() { return m_name; }
    float getXsize() { return m_Xsize; }
    float getYsize() { return m_Ysize; }
    float getArea()  { return m_area; }

    // Setters
    void setSize(float Xsize, float Ysize) { m_Xsize = Xsize; m_Ysize = Ysize; m_area = Xsize * Ysize; }

    // Pin offset accessors
    void addPinOffset(const string& pin_name, Position offset) { m_pin_offsets[pin_name] = offset; }
    bool hasPinOffset(const string& pin_name) const { return m_pin_offsets.count(pin_name) > 0; }
    const Position& getPinOffset(const string& pin_name) const { return m_pin_offsets.at(pin_name); }
    const std::map<string, Position>& getPinOffsets() const { return m_pin_offsets; }
    std::map<string, Position>& getPinOffsetsMutable() { return m_pin_offsets; }

}; // End of class MacroClass


AIEPLACE_NAMESPACE_END

#endif