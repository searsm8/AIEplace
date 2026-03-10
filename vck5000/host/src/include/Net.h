// TODO: add header
#ifndef AIEPLACE_NET_H
#define AIEPLACE_NET_H

#include "Common.h"
#include "Logger.h"
#include "Bin.h"

AIEPLACE_NAMESPACE_BEGIN
class Node;

struct NodePartial
{
    Gradient partial;
    Node* node_p; // Pointer to the node this partial belongs to
    NodePartial() : node_p(nullptr) { partial.clear(); }
    NodePartial(Node* node) : node_p(node) { partial.clear(); }
    
    void clear()
    {
        partial.clear();
    }
};

class Net
{
private:
    // Data members
    string m_name;
    int m_degree;


public:
    std::vector<Node*> mv_nodes; // List of all nodes on this net, sorted by descending X or Y positions
    std::map<Node*, string> mm_net_pins; // which pins are used for this net
    
    // TODO: use a vector of XY instead of a map
    // mm_partials_by_node is a map of Node pointers to XY partials
    // This is used to store the partials computed for each node in the net
    std::map<Node*, Gradient> mm_partials_by_node; // partials for each node
    std::vector<NodePartial> mv_partials; // partials for each node, used to accumulate results to be reduced later

    int tally = 0; // debugging counter used to track how many times this net has been processed

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
    
    void addNetPin(Node* n, string pin_str)
        { mm_net_pins[n] = pin_str; }
    
    void lockNodes()
    {
        //cout << "Locking nodes for net " << m_name << endl;
        TIME_FUNCTION();
        std::vector<Node*> locked_nodes;
        for(Node* node : mv_nodes) {
            if(std::count(locked_nodes.begin(), locked_nodes.end(), node) == 0)
            {
                locked_nodes.push_back(node);
                node->lock();
            }
        }
    }

    void unlockNodes()
    {
        //cout << "Unlocking nodes for net " << m_name << endl;
        std::vector<Node*> locked_nodes;
        for(Node* node : mv_nodes) {
            if(std::count(locked_nodes.begin(), locked_nodes.end(), node) == 0)
            {
                locked_nodes.push_back(node);
                node->unlock();
            }
        }
    }

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

    Box getBoundingBox();

    bool hasPin();

}; // End of class Net

AIEPLACE_NAMESPACE_END

#endif