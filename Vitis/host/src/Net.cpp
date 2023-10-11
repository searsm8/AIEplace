
#include "Net.h"
#include "Node.h"


AIEPLACE_NAMESPACE_BEGIN

struct Xgreater
{
    bool operator()( Node* l, Node* r ) {
        return l->getPosition().getX() > r->getPosition().getX();
    }
};

struct Ygreater
{
    bool operator()( Node* l, Node* r ) {
        return l->getPosition().getY() > r->getPosition().getY();
    }
};

/**
 * Sorts mv_nodes in descending order by X positions for feeding to AIE kernels
 */
void Net::sortPositionsByX()
{
    std::sort(mv_nodes.begin(), mv_nodes.end(), Xgreater());
}

/**
 * Sorts mv_nodes in descending order by Y positions for feeding to AIE kernels
 */
void Net::sortPositionsByY()
{
    std::sort(mv_nodes.begin(), mv_nodes.end(), Ygreater());
}

string Net::to_string()
{
    string s = m_name + ":\n";
    for (Node* node : mv_nodes)
    {
        if(node == nullptr)
        {
            cout << "nullptr found!" << endl;
            exit(1);
        }
        s += "\t" + node->getName() + " : " + node->getPosition().to_string() + "\n";
    }

    return s;
}

/**
 * Compute the wirelength of this net, using HPWL or RSMT method
 * 
 * @param method: name of the method to use. Either "HPWL" or "RSMT"
 * 
 * @return: The wirelength of the net.
 */
position_type Net::computeWirelength()
{
    if (WIRELENGTH_COMPUTE_METHOD == "HPWL")
        return computeWirelength_HPWL();
    else if (WIRELENGTH_COMPUTE_METHOD == "RSMT")
        return computeWirelength_RSMT();
    else
    {
        // Invalid, emit error
        cout << "ERROR: no wirelength method named " << WIRELENGTH_COMPUTE_METHOD << endl;
        exit(1);
    }
}

/** 
 * Simple Half-Perimeter Wirelength (HPWL)
 * 
 * @return: The wirelength of the net, computed using HPWL.
 */
position_type Net::computeWirelength_HPWL()
{
    sortPositionsByX();
    position_type width = mv_nodes.front()->getX() - mv_nodes.back ()->getX();
    sortPositionsByY();
    position_type height = mv_nodes.front()->getY() - mv_nodes.back ()->getY();
    return width + height;
}

/** 
 * Rectilinear Steiner Minimum Spanning Tree (RSMT)
 * 
 * @return: The wirelength of the net, computed using RSMT.
 */
position_type Net::computeWirelength_RSMT()
{
    position_type RSMT = 0;
    return RSMT;
}

AIEPLACE_NAMESPACE_END