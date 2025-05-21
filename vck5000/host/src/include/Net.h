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
private:
    // Data members
    string m_name;
    int m_degree;


public:
    std::vector<Node*> mv_nodes; // List of all nodes on this net, sorted by descending X or Y positions
    std::map<Node*, string> mm_net_pins; // which pins are used for this net
    std::map<Node*, XY> mm_partials_by_node; // partials for each node
    int tally = 0; // used to count how many times this net has been processed

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
    
    bool lockNodes()
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

    bool unlockNodes()
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

    Box<position_type> getBoundingBox();

    bool hasPin();

}; // End of class Net

AIEPLACE_NAMESPACE_END

#endif