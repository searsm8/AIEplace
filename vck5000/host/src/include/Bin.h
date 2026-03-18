// TODO: add header
#ifndef AIEPLACE_BOX_H
#define AIEPLACE_BOX_H

#include "Common.h"
#include "Node.h"
#include <sstream>

AIEPLACE_NAMESPACE_BEGIN

class Box
{
private:
    Position m_ll; // Lower left corner of Box
    Position m_ur; // Upper right corner of Box

public:
    // Constructors
    Box() {}
    Box(Position a, Position b) : m_ll(a), m_ur(b) {}
    Box(float xl, float yl, float xh, float yh) : m_ll(Position(xl, yl)), m_ur(Position(xh, yh)) {}

    // Member Functions
    // Getters
    Position getPos() { return m_ll; }
    Position getPosBottomLeft() const { return m_ll; }
    Position getPosTopRight() const { return m_ur; }
    float getXsize() { return abs(m_ll.x - m_ur.x); }
    float getYsize() { return abs(m_ll.y - m_ur.y); }
    float getArea()  { return getXsize() * getYsize(); }

    string to_string() {
        std::stringstream s;
        s << std::setprecision(2) << std::fixed;
        s << "Box@(" << m_ll.to_string() << ", " << m_ur.to_string() << ")";
        return s.str();
    }

    string getDEFstring() {
        std::stringstream s;
        s << " ( " << m_ll.x << " " << m_ll.y << " ) ( "
          << m_ur.x << " " << m_ur.y << " ) ";
        return s.str();
    }

    // Setters

};


// a Bin is a (relatively) small portion of the die area
// This struct collects useful data within the area of the Bounding Box
struct Bin
{
    Box bb; // Bounding Box
    float total_overlap; // Total Node overlap within this bin
    float a_uv;
    Gradient eField; // Computed eField in this Bin
    std::vector<Node*> overlapping_nodes; // list of nodes overlapping this bin
    float local_density_weight; // Local weight parameter for density in this bin.
                    // Bigger local_density_weight means the eField will push harder in this bin.

    Bin(float xl, float yl, float xh, float yh) : bb(xl, yl, xh, yh) {}

    void iterationReset() 
    {
        total_overlap = 0.0;
        overlapping_nodes.clear();
        a_uv = 0;
        eField.x = 0; eField.y = 0;
    }

    /* @brief: Computes the overlap of the given node and this bin
        *         Adds the result to overlap
        */
    void computeOverlap(Node* node_p)
    {
        double overlap_width =
            min((double)bb.getPosTopRight().x, ((double)node_p->next.node_pos.x + (double)node_p->getXsize()))
            - max((double)bb.getPosBottomLeft().x, (double)node_p->next.node_pos.x) ;
        double overlap_height =
            min((double)bb.getPosTopRight().y, ((double)node_p->next.node_pos.y + (double)node_p->getYsize()))
            - max((double)bb.getPosBottomLeft().y, (double)node_p->next.node_pos.y);

        //cout << "overlap_width: " << overlap_width << "\n";
        //cout << "bb.getPosTopRight().y = " << (float)bb.getPosTopRight().y << endl;
        //cout << "node top right Y = " << (float)node_p->next.node_pos.y + (float)node_p->getYsize() << endl;
        //cout << "bb.getPosBottomLeft().y =" << (float)bb.getPosBottomLeft().y << endl;
        //cout << "node bottom left Y = " << (float)node_p->next.node_pos.y << endl;
        //cout << "overlap_height: " << overlap_height<< "\n";

        //assert(abs(overlap_width)  > node_p->getXsize() && "abs(overlap) exceeds node width!");
        //assert(abs(overlap_height) > node_p->getYsize() && "abs(overlap) exceeds node height!");
        
        double node_overlap = overlap_width * overlap_height;

        // If this node has a non-zero overlap with in this bin, add to list
        if (node_overlap > 0)
        {
            total_overlap += node_overlap;
            overlapping_nodes.push_back(node_p);
            node_p->addBinOverlap(this, node_overlap);
        } 
    }

    float getOverlap() {
        return total_overlap;
    }

    float getOverflowRatio()
    {
        //return max<float>(0, total_overlap - bb.getArea());
        return max<float>(0, total_overlap / bb.getArea() - 1);
    }
};

AIEPLACE_NAMESPACE_END

#endif