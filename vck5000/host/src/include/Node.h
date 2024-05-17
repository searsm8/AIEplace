// TODO: add header
#ifndef AIEPLACE_NODE_H
#define AIEPLACE_NODE_H

#include "Common.h"
#include "Position.h"
#include "MacroClass.h"

AIEPLACE_NAMESPACE_BEGIN

class Net;
class Bin;

enum PlacementStatus
{
    UNPLACED = 0x0, 
    PLACED = 0x1, 
    FIXED = 0x2, 
    DUMMY_FIXED = 0x3, ///< initially not fixed, but fixed by the placer 
    UNKNOWN = 0x4 
};

struct BinOverlap
{
    Bin* bin;
    double overlap;
};


class Node
{
private:
    // For use in computations
    struct Term
    {
        XY plus;
        XY minus;
        void clear() { plus.clear(); minus.clear(); }
    };

    struct Terms
    {
        Term a;
        XY partials;
        void clear() { a.clear(); partials.clear(); }
    };
    // Data members
    string m_name;
    Position<position_type> m_pos;
    PlacementStatus m_status;
    std::vector<Net*> mv_nets; // List of all nets this node is on
    std::vector<BinOverlap> mv_bin_overlaps; // List of bins this node is currently overlapping

public:

    Terms terms;
    Terms terms_aie; // DEBUG: used to compare AIE with CPU results

    // Constructors
    Node() : m_name("") {}
    Node(string name) : m_name(name), m_pos(0, 0) {}

    // Member Functions
    // Getters
    const string& getName() { return m_name; }
    const PlacementStatus& getStatus() { return m_status; }
    Position<position_type> getPosition() { return m_pos; }
    void translate(double move_x, double move_y) { m_pos.translate(move_x, move_y); }
    const position_type& getX() { return m_pos.getX(); }
    const position_type& getY() { return m_pos.getY(); }

    std::vector<Net*> getNets() { return mv_nets; }
    std::vector<BinOverlap> getBinOverlaps() { return mv_bin_overlaps; }

    // Setters
    void iterationReset()
    {
        mv_bin_overlaps.clear();
        terms.clear();
        terms_aie.clear();
    }

    void addNet(Net* net_p) { mv_nets.push_back(net_p); }

    void addBinOverlap(Bin* bin_p, double node_overlap) 
    {
        BinOverlap b;
        b.bin = bin_p;
        b.overlap = node_overlap;
        mv_bin_overlaps.push_back(b);
    }

    void setPosition(Position<position_type> pos) { m_pos = pos; }
    void setPlacementStatus(PlacementStatus status) { m_status = status; }
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

    virtual double getXsize() { return 0.0; }
    virtual double getYsize() { return 0.0; }
    virtual double getArea()  { return 0.0; }

    void printTerms() {
        cout << "Node " << m_name << ":";
        cout << "\ta+x: " << terms.a.plus.x;
        cout << "\ta-x: " << terms.a.minus.x;
        cout << "\ta+y: " << terms.a.plus.y;
        cout << "\ta-y: " << terms.a.minus.y;
        cout << "\tpartial_x: " << terms.partials.x;
        cout << "\tpartial_y: " << terms.partials.y;
        cout << endl;

    }

}; // End of class Node


AIEPLACE_NAMESPACE_END

#endif