// TODO: add header
#ifndef AIEPLACE_NET_H
#define AIEPLACE_NET_H

#include "AIEplace.h"

AIEPLACE_NAMESPACE_BEGIN
class Node;

class Net
{
private:
    // Data members
    string m_name;
    std::vector<Node*> mv_nodes; // List of all nodes on this net

public:
    // Constructors
    Net() {}
    Net(string name) : m_name(name) {}

    // Member Functions

    // Getters
    string name() { return m_name; }
    std::vector<Node*> nodes() { return mv_nodes; }

    // Setters
    void addNode(Node* n) { mv_nodes.push_back(n); }

    // Sorting
    std::vector<position_type> get_X_coords_descending();
    std::vector<position_type> get_Y_coords_descending();
    

}; // End of class Net

AIEPLACE_NAMESPACE_END

#endif