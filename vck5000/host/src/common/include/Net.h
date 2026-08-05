/**
 * @file Net.h
 * @brief A net (hyperedge) and its pins. Owns the node list the wirelength gradient
 *        and HPWL are computed over.
 */
#pragma once

#include "Common.h"
#include "Logger.h"
#include "Bin.h"
#include "Node.h"

AIEPLACE_NAMESPACE_BEGIN

/**
 * @brief A pin connection on a net: the parent node, the pin offset from the node origin,
 *        and the pin name. Absolute pin position is node position + offset —
 *        getProbePos() for gradient evaluation, getPos() for the committed position.
 */
struct NetPin {
    Node* node_p;
    Position offset;   // from MacroClass pin geometry; (0,0) if unknown
    string pin_name;   // DEF pin name (e.g. "A", "Y"); empty for IOPads/bookshelf

    Position getPos() const { return node_p->getPos() + offset; }
    Position getProbePos() const { return node_p->getProbePos() + offset; }
};

/// @brief A hyperedge over nodes, with parallel node and pin lists.
class Net
{
public:
    // Data members
    string m_name;
    int m_degree;

    std::vector<Node*> mv_nodes; // List of all nodes on this net
    std::vector<NetPin> mv_pins; // Parallel to mv_nodes: each node + pin offset + pin name

    // Constructors
    Net() : m_degree(0) {}
    Net(string name) : m_name(name), m_degree(0) {}

    // Member Functions

    // Getters
    string getName() { return m_name; }
    const std::vector<Node*>& getNodes() { return mv_nodes; }
    const std::vector<NetPin>& getPins() { return mv_pins; }
    std::vector<NetPin>& getPinsMutable() { return mv_pins; }
    int getDegree() { return m_degree; }

    void addNode(Node* n, Position pin_offset = Position(0, 0), string pin_name = "")
        { mv_nodes.push_back(n); mv_pins.push_back({n, pin_offset, pin_name}); m_degree++; }

    // Sorting
    void sortPositionsByX();
    void sortPositionsByY();

    // Metrics
    position_type computeWirelength(string method); // method must be "HPWL" or "RSMT"
    position_type computeWirelength_HPWL(); // Simple Half-Perimeter Wirelength
    position_type computeWirelength_RSMT(); // Rectilinear Steiner Minimum Spanning Tree

    // Debugging
    string to_string();

    Box getBoundingBox();

    bool hasIOPad();
    bool hasFixedNode();

}; // End of class Net

AIEPLACE_NAMESPACE_END

