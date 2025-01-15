
#include "Net.h"
#include "Node.h"


AIEPLACE_NAMESPACE_BEGIN

struct Xgreater
{
    bool operator()( Node* l, Node* r ) {
        return l->getPosition().getX() > r->getPosition().getX();
    }
};

struct Xlesser
{
    bool operator()( Node* l, Node* r ) {
        return l->getPosition().getX() < r->getPosition().getX();
    }
};

struct Ygreater
{
    bool operator()( Node* l, Node* r ) {
        return l->getPosition().getY() > r->getPosition().getY();
    }
};

struct Ylesser
{
    bool operator()( Node* l, Node* r ) {
        return l->getPosition().getY() < r->getPosition().getY();
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

/**
 * Sort mv_nodes for AIE kernel execution
 * max_x, min_x, x, x, x...
 */
void Net::sortPositionsMaxMinX()
{
    sortPositionsByX();
    // Put the min value in 2nd position
    std::swap(mv_nodes.at(1), mv_nodes.back());

    // For AIE kernels, data format requires 1st element being max and 2nd element being min
    // other coordinates for nodes on the net can be in any order
}

/**
 * Sort mv_nodes for AIE kernel execution
 * max_y, min_y, y, y, y...
 */
void Net::sortPositionsMaxMinY()
{
    sortPositionsByY();
    // Put the min value in 2nd position
    std::swap(mv_nodes.at(1), mv_nodes.back());

    // For AIE kernels, data format requires 1st element being max and 2nd element being min
    // other coordinates for nodes on the net can be in any order
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
        s += "\t" + node->getName() + " (Pin " + mm_net_pins[node] + ") : " + node->getPosition().to_string() + "\n";
    }

    return s;
}

// TODO: remove unused function?
void Net::printTerms()
{
    cout << "Terms of net " << m_name << ":" << endl;
    cout << "\tb+x: " << terms_cpu.b.plus.x;
    cout << "\tb-x: " << terms_cpu.b.minus.x;
    cout << "\tc+x: " << terms_cpu.c.plus.x;
    cout << "\tc-x: " << terms_cpu.c.minus.x;
    cout << endl;

    for(Node* np : mv_nodes)
        np->printTerms();
}

/**
 * Compute the wirelength of this net, using HPWL or RSMT method
 * 
 * @param method: name of the method to use. Either "HPWL" or "RSMT"
 * 
 * @return: The wirelength of the net.
 */
position_type Net::computeWirelength(string method)
{
    if (method == "HPWL")
        return computeWirelength_HPWL();
    else if (method == "RSMT")
        return computeWirelength_RSMT();
    else
    {
        // Invalid, emit error
        cout << "ERROR: no wirelength method named " << method << endl;
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
    position_type width = mv_nodes.front()->getX() - mv_nodes.back()->getX();
    //cout << "front: " << mv_nodes.front()->getX() << "\tback: " << mv_nodes.back()->getX() << endl;
    sortPositionsByY();
    position_type height = mv_nodes.front()->getY() - mv_nodes.back()->getY();
    // check if nan
    //if(width != width || height != height)
    //{
    //    cout << endl << "Net: " << m_name << endl;
    //    cout << "front: " << mv_nodes.front()->getX() << " : " << mv_nodes.front()->getY() << endl;
    //    cout << "back: " << mv_nodes.back()->getX() << " : " << mv_nodes.back()->getY() << endl;
    //    cout << "Height: " << height << "\tWidth: " << width << "\tHPWL: " << height+width << endl;
    //    cout << "nodes sorted by Y: " << endl; 
    //    for(auto np : mv_nodes)
    //        np->printXY();
    //}
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