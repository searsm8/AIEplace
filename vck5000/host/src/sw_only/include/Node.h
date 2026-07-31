/**
 * @file Node.h
 * @brief Abstract placeable object (Component or IOPad): its position, iteration state,
 *        net memberships, and bin overlaps. The unit the placement gradient acts on.
 */
#pragma once

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

/// @brief A node's overlap with one bin: the bin and the deposited area.
struct BinOverlap
{
    Bin* bin_p;
    double overlap;
};


class Node
{
private:
    // Data members
    string m_name;
    string m_orient;
    PlacementStatus m_status = UNKNOWN;
    bool m_is_large = false; // True if the area of the node is at least 1/16th the area of a bin
    bool m_is_movable_macro = false; // XPlace is_mov_macro rule; see Placer::tagMovableMacros
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
    float precond_weight = 1.0f; // diagonal preconditioner: grad is divided by this before stepping

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
    Position getPos() { return next.node_pos; }
    Position getProbePos() { return next.probe_pos; }
    float getX() { return next.node_pos.x; }
    float getY() { return next.node_pos.y; }
    float getProbeX() { return next.probe_pos.x; }
    float getProbeY() { return next.probe_pos.y; }
    void setX(float x) { next.node_pos.x = x; }
    void setY(float y) { next.node_pos.y = y; }

    void translate(float move_x, float move_y) { next.node_pos.translate(move_x, move_y); }
    void translate(XY move) { next.node_pos.translate(move.x, move.y); }

    std::vector<Net*>& getNets() { return mv_nets; }
    std::vector<BinOverlap>& getBinOverlaps() { return mv_bin_overlaps; }

    void iterationReset()
    {
        mv_bin_overlaps.clear();
    }

    // Promote next → current for the new iteration
    void cacheState() { current = next; }

    // Roll next back to the cached anchor (inverse of cacheState); used to undo a probe step
    void restoreState() { next = current; }

    // Perform Nesterov gradient step (Algorithm 1, Lines 2 & 4)
    // Reads from current (must call cacheState() first), writes to next
    // Gradient is preconditioned (divided by precond_weight) before stepping
    void step(float step_length, float momentum_coeff) {
        // Precondition: scale gradient by 1/precond_weight (diagonal preconditioner)
        Gradient precond_grad = (1.0f / precond_weight) * current.probe_grad;
        // Line 2: u_{k+1} = v_k - α · P·∇f(v_k)
        next.node_pos = current.probe_pos - step_length * precond_grad;
        // Line 4: v_{k+1} = u_{k+1} + mom · (u_{k+1} - u_k)
        next.probe_pos = next.node_pos + momentum_coeff * (next.node_pos - current.node_pos);
    }

    void addNet(Net* net_p) { mv_nets.push_back(net_p); }

    void addBinOverlap(Bin* bin_p, double node_overlap)
    {
        BinOverlap b;
        b.bin_p = bin_p;
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

    // Movable-macro tag under XPlace's is_mov_macro rule (Placer::tagMovableMacros). Distinct from
    // the coarser die-area heuristic behind Placer::num_movable_macros — see TODO #11.
    void setMovableMacro(bool is_macro) { m_is_movable_macro = is_macro; }
    bool isMovableMacro() { return m_is_movable_macro; }

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

