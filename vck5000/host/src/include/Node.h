// TODO: add header
#ifndef AIEPLACE_NODE_H
#define AIEPLACE_NODE_H

#include "Common.h"
#include "Position.h"
#include "MacroClass.h"
//#include "Logger.h"

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
    string m_orient;
    std::mutex m_mutex;
    Position<position_type> m_pos; // u_k, actual location of the node
    Position<position_type> m_lookahead_pos; // v_k
    Position<position_type> m_prev_lookahead_pos; // v_{k-1}
    Position<position_type> m_prev_grad; // grad(v_{k-1})

    PlacementStatus m_status;
    bool m_is_large; // True if the area of the node is at least 1/16th the area of a bin
    std::vector<Net*> mv_nets; // List of all nets this node is on
    std::vector<BinOverlap> mv_bin_overlaps; // List of bins this node is currently overlapping

public:

    XY partials_aie; // Computed on AIE by default 
    Terms terms_cpu; // DEBUG: used to compare AIE with CPU results
    XY combined_force; // electro_force - partials for BB denominator

    // Constructors
    Node() : m_name("") {}
    Node(string name) : m_name(name), m_orient("N"), m_pos(0, 0), 
            m_prev_lookahead_pos(0, 0), m_prev_grad(0, 0), combined_force() 
            {}

    // Need virtual destructor for proper dynamic_cast
    virtual ~Node() {} // Destructor

    // Member Functions
    // Getters
    const string& getName() { return m_name; }
    const PlacementStatus& getStatus() { return m_status; }
    Position<position_type>& getPosition() { return m_pos; } // return a reference to avoid copying
    Position<position_type>& getPrevLookaheadPosition() { return m_prev_lookahead_pos; } 
    Position<position_type>& getLookaheadPosition() { return m_lookahead_pos; } 
    Position<position_type>& getPrevGrad() { return m_prev_grad; } 
    void translate(float move_x, float move_y) { m_pos.translate(move_x, move_y); }
    void translate(XY move) { m_pos.translate(move.x, move.y); }
    const string& getOrientation() { return m_orient; }
    const position_type& getX() { return m_pos.getX(); }
    const position_type& getY() { return m_pos.getY(); }
    void setX(float x) { m_pos.setX(x); }
    void setY(float y) { m_pos.setY(y); }
    void lock() {  m_mutex.lock(); }
    void unlock() {  m_mutex.unlock();  }

    std::vector<Net*> getNets() { return mv_nets; }
    std::vector<BinOverlap> getBinOverlaps() { return mv_bin_overlaps; }

    // Setters
    void iterationReset()
    {
        mv_bin_overlaps.clear();
        terms_cpu.clear();
        partials_aie.clear();
    }

    void addNet(Net* net_p) { mv_nets.push_back(net_p); }

    void addBinOverlap(Bin* bin_p, double node_overlap) 
    {
        BinOverlap b;
        b.bin = bin_p;
        b.overlap = node_overlap;
        mv_bin_overlaps.push_back(b);
    }

    bool checkIfLarge(float threshold)
    {
        m_is_large = this->getArea() > threshold;
        return m_is_large;
    }

    bool isLarge() { return m_is_large; }
    bool isPlaced() { return m_status == PLACED; }

    void setPosition(Position<position_type> pos) { m_pos = pos; }
    void setPlacementStatus(PlacementStatus status) { m_status = status; }
    void setOrientation(string orient) { m_orient = orient; }
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

    // pure virtual functions
    // these must be implemented in derived classes
    // this makes Node an abstract class
    virtual float getXsize() = 0;
    virtual float getYsize() = 0;
    virtual float getArea()  = 0;

// TODO: remove unused function?
    void printXY() {
        cout << "Node " << m_name << ": (" << m_pos.getX() << ", " << m_pos.getY() << ")" << endl;

    }

    void printTerms() {
        cout << "Node " << m_name << ":";
        cout << "\ta+x: " << terms_cpu.a.plus.x;
        cout << "\ta-x: " << terms_cpu.a.minus.x;
        cout << "\ta+y: " << terms_cpu.a.plus.y;
        cout << "\ta-y: " << terms_cpu.a.minus.y;
        cout << "\tpartial_x: " << terms_cpu.partials.x;
        cout << "\tpartial_y: " << terms_cpu.partials.y;
        cout << endl;
    }

}; // End of class Node



AIEPLACE_NAMESPACE_END

#endif