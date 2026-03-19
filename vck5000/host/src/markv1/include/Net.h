// TODO: add header
#ifndef AIEPLACE_NET_H
#define AIEPLACE_NET_H

#include "Common.h"
#include "Logger.h"
#include "Bin.h"

AIEPLACE_NAMESPACE_BEGIN
class Node;

class Net
{
public:
    // Data members
    string m_name;
    int m_degree;


    std::vector<Node*> mv_nodes; // List of all nodes on this net, sorted by descending X or Y positions
    std::map<Node*, string> mm_net_pins; // which pins are used for this net

    int tally = 0; // debugging counter used to track how many times this net has been processed

    // Constructors
    Net() : m_degree(0) {}
    Net(string name) : m_name(name), m_degree(0) {}

    // Member Functions

    // Getters
    string getName() { return m_name; }
    const std::vector<Node*>& getNodes() { return mv_nodes; }
    int getDegree() { return m_degree; }

    void addNode(Node* n)
        { mv_nodes.push_back(n); m_degree++; }

    void addNetPin(Node* n, string pin_str)
        { mm_net_pins[n] = pin_str; }

    // Sorting
    void sortPositionsByX();
    void sortPositionsByY();
    void sortPositionsMaxMinX();
    void sortPositionsMaxMinY();

    // Metrics
    position_type computeWirelength(string method); // method must be "HPWL" or "RSMT"
    position_type computeWirelength_HPWL(); // Simple Half-Perimeter Wirelength
    position_type computeWirelength_RSMT(); // Rectilinear Steiner Minimum Spanning Tree

    // Debugging
    string to_string();

    Box getBoundingBox();

    bool hasPin();

}; // End of class Net

AIEPLACE_NAMESPACE_END

#endif
