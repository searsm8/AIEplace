// TODO: add header
#ifndef AIEPLACE_NODE_H
#define AIEPLACE_NODE_H

#include "Common.h"
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
    // Data members
    string m_name;
    string m_orient;
    std::mutex m_mutex;
    PlacementStatus m_status;
    bool m_is_large; // True if the area of the node is at least 1/16th the area of a bin
    std::vector<Net*> mv_nets; // List of all nets this node is on
    std::vector<BinOverlap> mv_bin_overlaps; // List of bins this node is currently overlapping

public:

    // Iteration state: current = iteration k (anchor), next = iteration k+1 (trial)
    struct State
    {
        Position node_pos;   // u — actual/committed position
        Position probe_pos;  // v — lookahead/probe position
        Gradient probe_grad; // ∇f(v) — starts as HPWL-only, becomes total after combineGradients()
    };

    State current;  // iteration k (frozen during backtracking)
    State next;     // iteration k+1 (trial during backtracking)
    Position best_solution_pos;  // stored best solution found, prevents divergence regret

    // Constructors
    Node() : m_name("") {}
    Node(string name) : m_name(name), m_orient("N"), current{}, next{} {}

    // Need virtual destructor for proper dynamic_cast
    virtual ~Node() {} // Destructor

    // Member Getter Functions
    const string& getName() { return m_name; }
    const PlacementStatus& getStatus() { return m_status; }
    const string& getOrientation() { return m_orient; }

    // Convenience getters (for code that doesn't care about current/next distinction)
    float getX() { return next.node_pos.x; }
    float getY() { return next.node_pos.y; }
    float getProbeX() { return next.probe_pos.x; }
    float getProbeY() { return next.probe_pos.y; }
    void setX(float x) { next.node_pos.x = x; }
    void setY(float y) { next.node_pos.y = y; }

    void translate(float move_x, float move_y) { next.node_pos.translate(move_x, move_y); }
    void translate(XY move) { next.node_pos.translate(move.x, move.y); }

    void lock() {  m_mutex.lock(); }
    void unlock() {  m_mutex.unlock();  }

    std::vector<Net*>& getNets() { return mv_nets; }
    std::vector<BinOverlap>& getBinOverlaps() { return mv_bin_overlaps; }

    void iterationReset()
    {
        mv_bin_overlaps.clear();
    }

    // Promote next → current for the new iteration
    void cacheState() { current = next; }

    // Perform Nesterov gradient step (Algorithm 1, Lines 2 & 4)
    // Reads from current (must call cacheState() first), writes to next
    void step(float step_length, float momentum_coeff) {
        // Line 2: u_{k+1} = v_k - α · ∇f(v_k)
        next.node_pos = current.probe_pos - step_length * current.probe_grad;
        // Line 4: v_{k+1} = u_{k+1} + mom · (u_{k+1} - u_k)
        next.probe_pos = next.node_pos + momentum_coeff * (next.node_pos - current.node_pos);
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

    void setNodePos(Position pos) { next.node_pos = pos; }
    void initializeState(Position pos) { current.node_pos = pos; current.probe_pos = pos; 
                                         next.node_pos = pos; next.probe_pos = pos; }
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

    void printXY() {
        cout << "Node " << m_name << ": (" << next.node_pos.x << ", " << next.node_pos.y << ")" << endl;
    }

}; // End of class Node



AIEPLACE_NAMESPACE_END

#endif
