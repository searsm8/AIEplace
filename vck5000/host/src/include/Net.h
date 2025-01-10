// TODO: add header
#ifndef AIEPLACE_NET_H
#define AIEPLACE_NET_H

#include "Common.h"

AIEPLACE_NAMESPACE_BEGIN
class Node;

class Net
{
private:
    // Data members
    string m_name;
    int m_degree;

    // List of all nodes on this net, sorted by descending X or Y positions

public:
    std::vector<Node*> mv_nodes;
    // For use in computations
    struct XY
    {
        double x;
        double y;
        void clear() { x = 0; y = 0;}
    };

    struct Term
    {
        XY plus;
        XY minus;
        void clear() { plus.clear(); minus.clear(); }
    };

    struct Terms
    {
        Term b;
        Term c;
        void clear() { b.clear(); c.clear(); }
    };

    Terms terms_cpu;

    // Constructors
    Net() : m_degree(0) {}
    Net(string name) : m_name(name), m_degree(0) {}

    // Member Functions

    // Getters
    string getName() { return m_name; }
    const std::vector<Node*>& getNodes() { return mv_nodes; }
    int getDegree() { return m_degree; }

    // Setters
    void iterationReset()
    {
        terms_cpu.clear();
    }

    void addNode(Node* n) 
        { mv_nodes.push_back(n); m_degree++; }

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
    void printTerms();

}; // End of class Net

AIEPLACE_NAMESPACE_END

#endif