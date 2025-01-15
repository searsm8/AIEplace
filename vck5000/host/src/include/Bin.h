// TODO: add header
#ifndef AIEPLACE_BOX_H
#define AIEPLACE_BOX_H

#include "Common.h"
#include "Position.h"
#include "Node.h"
#include <sstream>

AIEPLACE_NAMESPACE_BEGIN

template<typename T>
class Box 
{
private:
    Position<T> m_ll; // Lower left corner of Box
    Position<T> m_ur; // Upper right corner of Box

public:
    // Constructors
    Box() {}
    Box(Position<T> a, Position<T> b) : m_ll(a), m_ur(b) {}
    Box(T xl, T yl, T xh, T yh) : m_ll(Position<T>(xl, yl)), m_ur(Position<T>(xh, yh)) {}

    // Member Functions
    // Getters
    Position<T> getPos() { return m_ll; }
    Position<T> getPosBottomLeft() const { return m_ll; }
    Position<T> getPosTopRight() const { return m_ur; }
    T getXsize() { return abs(m_ll.getX() - m_ur.getX()); }
    T getYsize() { return abs(m_ll.getY() - m_ur.getY()); }
    T getArea()  { return getXsize() * getYsize(); }

    string to_string() {
        std::stringstream s;
        s << std::setprecision(2) << std::fixed;
        s << "Box@(" << m_ll.to_string() << ", " << m_ur.to_string() << ")";
        return s.str();
    }

    string getDEFstring() {
        std::stringstream s;
        s << " ( " << m_ll.getX() << " " << m_ll.getY() << " ) ( " 
          << m_ur.getX() << " " << m_ur.getY() << " ) ";
        return s.str();
    }

    // Setters

};


// a Bin is a (relatively) small portion of the die area
// This struct collects useful data within the area of the Bounding Box
struct Bin
{
    Box<float> bb; // Bounding Box
    float overlap; // Total Node overlap within this bin
    float a_uv;
    XY eField; // Computed eField in this Bin
    std::vector<Node*> overlapping_nodes; // list of nodes overlapping this bin
    float lambda; // Local Weight parameter for density in this bin. 
                    // Bigger lambda means the eField will push harder in this bin.

    Bin(float xl, float yl, float xh, float yh) : bb(xl, yl, xh, yh) {}

    void iterationReset() 
    {
        overlap = 0.0;
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
            min((double)bb.getPosTopRight().getX(), ((double)node_p->getPosition().getX() + (double)node_p->getXsize()))
            - max(bb.getPosBottomLeft().getX(), node_p->getPosition().getX()) ;
        double overlap_height = 
            min((double)bb.getPosTopRight().getY(), ((double)node_p->getPosition().getY() + (double)node_p->getYsize()))
            - max(bb.getPosBottomLeft().getY(), node_p->getPosition().getY());

        //cout << "overlap_width: " << overlap_width << "\n";
        //cout << "bb.getPosTopRight().getY() = " << (float)bb.getPosTopRight().getY() << endl;
        //cout << "node top right Y = " << (float)node_p->getPosition().getY() + (float)node_p->getYsize() << endl;
        //cout << "bb.getPosBottomLeft().getY() =" << (float)bb.getPosBottomLeft().getY() << endl;
        //cout << "node bottom left Y = " << (float)node_p->getPosition().getY()<< endl;
        //cout << "overlap_height: " << overlap_height<< "\n";

        //assert(abs(overlap_width)  > node_p->getXsize() && "abs(overlap) exceeds node width!");
        //assert(abs(overlap_height) > node_p->getYsize() && "abs(overlap) exceeds node height!");
        
        double node_overlap = overlap_width * overlap_height;
        overlap += node_overlap;

        // If this node has a non-zero overlap with in this bin, add to list
        if (node_overlap > 0)
        {
            overlapping_nodes.push_back(node_p);
            node_p->addBinOverlap(this, node_overlap);
        } else if (node_overlap < 0) {
            overlapping_nodes.push_back(node_p);
            node_p->addBinOverlap(this, node_overlap);
            Table top;
            top.add_row(RowStream{} << "Negative overlap detected for bin" << "");
            top.add_row(RowStream{} << "Bottom Left" << "Top Right");
            top.add_row(RowStream{} << bb.getPosBottomLeft().to_string()<< bb.getPosTopRight().to_string());
            Table t;
            t.add_row(RowStream{} << "name" << "X" << "Y" << "area" << "width" << "height" << "overlap" << "width" << "height");
            t.add_row(RowStream{} << node_p->getName() << node_p->getX() << node_p->getY() << node_p->getArea() << node_p->getXsize() << node_p->getYsize() << node_overlap << overlap_width << overlap_height);
            top.add_row({t});
            log("WARNING", top);
        }



    }

    float computeOverflow()
    {
        return max<float>(0, overlap - bb.getArea());
    }
};

AIEPLACE_NAMESPACE_END

#endif