/**
 * @file Bin.h
 * @brief Geometry primitives: Box (an axis-aligned rectangle) and Bin (one density-grid cell).
 */
#pragma once

#include "Common.h"
#include "Node.h"
#include <sstream>

AIEPLACE_NAMESPACE_BEGIN

/**
 * @brief An axis-aligned rectangle, stored by its lower-left and upper-right corners.
 *        Used for die areas, bin bounding boxes, and net bounding boxes.
 */
class Box
{
private:
    Position m_lower_left;   // lower-left corner
    Position m_upper_right;  // upper-right corner

public:
    // Constructors
    Box() {}
    Box(Position lower_left, Position upper_right) : m_lower_left(lower_left), m_upper_right(upper_right) {}
    Box(float xl, float yl, float xh, float yh) : m_lower_left(Position(xl, yl)), m_upper_right(Position(xh, yh)) {}

    // Getters
    Position getPos() { return m_lower_left; }
    Position getPosBottomLeft() const { return m_lower_left; }
    Position getPosTopRight() const { return m_upper_right; }
    float getXsize() { return abs(m_lower_left.x - m_upper_right.x); }
    float getYsize() { return abs(m_lower_left.y - m_upper_right.y); }
    float getArea()  { return getXsize() * getYsize(); }

    /// @brief Human-readable "Box@(lower_left, upper_right)" for logging.
    string to_string() {
        std::stringstream s;
        s << std::setprecision(2) << std::fixed;
        s << "Box@(" << m_lower_left.to_string() << ", " << m_upper_right.to_string() << ")";
        return s.str();
    }

    /// @brief DEF-format rectangle string: " ( xl yl ) ( xh yh ) ".
    string getDEFstring() {
        std::stringstream s;
        s << " ( " << m_lower_left.x << " " << m_lower_left.y << " ) ( "
          << m_upper_right.x << " " << m_upper_right.y << " ) ";
        return s.str();
    }
};


/**
 * @brief One cell of the density grid: a bounding box plus the per-bin quantities the
 *        electrostatic density solver accumulates (deposited area, DCT coefficient, field).
 */
struct Bin
{
    Box bb;                                 // bounding box
    float total_overlap = 0.0f;             // total node area deposited in this bin
    float a_uv = 0.0f;                      // DCT coefficient a_uv for this bin
    Gradient eField;                        // electric field (Ex, Ey) solved for this bin
    std::vector<Node*> overlapping_nodes;   // nodes overlapping this bin
    float local_density_weight = 0.0f;      // per-bin density weight; larger => stronger push

    Bin(float xl, float yl, float xh, float yh) : bb(xl, yl, xh, yh) {}

    /// @brief Clear per-iteration accumulators (called before each density solve).
    void iterationReset()
    {
        total_overlap = 0.0;
        overlapping_nodes.clear();
        a_uv = 0;
        eField.x = 0; eField.y = 0;
    }

    /**
     * @brief Accumulate the overlap area between @p node_p and this bin.
     *        On a positive overlap, records the node and registers the reciprocal
     *        BinOverlap on the node (so the field can later be gathered back).
     * @param node_p node evaluated at its committed position (next.node_pos)
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

