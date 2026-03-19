#include "Grid.h"
#include "Logger.h"

AIEPLACE_NAMESPACE_BEGIN

void Grid::init()
{
    m_bin_width  = m_die_area.getXsize() / (float)m_bins_per_row;
    m_bin_height = m_die_area.getYsize() / (float)m_bins_per_col;

    for( int x_index = 0; x_index < m_bins_per_row; x_index++)
    {
        m_bins.push_back(std::vector<Bin>());
        for( int y_index = 0; y_index < m_bins_per_col; y_index++)
        {
            Bin b = Bin(x_index*m_bin_width, y_index*m_bin_height, 
                   (x_index+1)*m_bin_width, (y_index+1)*m_bin_height);
            b.local_density_weight = INITIAL_LOCAL_DENSITY_WEIGHT;
            m_bins[x_index].push_back(b);
        }
    }
}

/* @brief: Reset all nodes and nets for the next iteration.
*/
void Grid::iterationReset()
{
    for( int x_index = 0; x_index < m_bins_per_row; x_index++)
        for( int y_index = 0; y_index < m_bins_per_col; y_index++)
            m_bins[x_index][y_index].iterationReset();
}

/**
 * @brief Distribute a node's area across all grid bins it overlaps.
 *
 * For each bin the node's bounding box intersects, compute the rectangular
 * intersection area and add it to that bin's total_overlap. Also records the
 * per-bin overlap on the node (for electrostatic force computation).
 *
 * Small nodes that fit entirely within one bin get the fast path (no
 * intersection math — area goes directly to the single bin). Large nodes
 * that span multiple bins get the exact intersection area for each bin.
 *
 * @param node_p Pointer to the node whose overlap to distribute.
 */
void Grid::computeBinOverlaps(Node* node_p)
{
    // Node bounding box (lower-left anchor at probe position)
    float node_xl = node_p->getProbeX();
    float node_yl = node_p->getProbeY();
    float node_xh = node_xl + node_p->getXsize();
    float node_yh = node_yl + node_p->getYsize();

    // Bin index range the node spans (clamped to grid bounds)
    int col_lo = std::max(0, (int)(node_xl / m_bin_width));
    int col_hi = std::min(m_bins_per_row - 1, (int)(node_xh / m_bin_width));
    int row_lo = std::max(0, (int)(node_yl / m_bin_height));
    int row_hi = std::min(m_bins_per_col - 1, (int)(node_yh / m_bin_height));

    // Fast path: node fits in a single bin — skip intersection math
    if (col_lo == col_hi && row_lo == row_hi) {
        float area = node_p->getArea();
        Bin& bin = m_bins[col_lo][row_lo];
        bin.total_overlap += area;
        bin.overlapping_nodes.push_back(node_p);
        node_p->addBinOverlap(&bin, area);
        return;
    }

    // General case: node spans multiple bins — compute exact intersection
    for (int col = col_lo; col <= col_hi; col++) {
        float bin_xl = col * m_bin_width;
        float bin_xh = bin_xl + m_bin_width;

        // Intersection width = overlap of [node_xl, node_xh] and [bin_xl, bin_xh]
        float overlap_xl = std::max(node_xl, bin_xl);
        float overlap_xh = std::min(node_xh, bin_xh);
        float overlap_w  = overlap_xh - overlap_xl;
        if (overlap_w <= 0) continue;

        for (int row = row_lo; row <= row_hi; row++) {
            float bin_yl = row * m_bin_height;
            float bin_yh = bin_yl + m_bin_height;

            // Intersection height
            float overlap_yl = std::max(node_yl, bin_yl);
            float overlap_yh = std::min(node_yh, bin_yh);
            float overlap_h  = overlap_yh - overlap_yl;
            if (overlap_h <= 0) continue;

            float overlap_area = overlap_w * overlap_h;
            Bin& bin = m_bins[col][row];
            bin.total_overlap += overlap_area;
            bin.overlapping_nodes.push_back(node_p);
            node_p->addBinOverlap(&bin, overlap_area);
        }
    }
}

std::vector< std::vector<float> > Grid::getBinDensities()
{
    // Compute per-bin density (rho = total_overlap / bin_area)
    std::vector< std::vector<float> > density;
    float bin_area_inv = 1 / m_bins[0][0].bb.getArea();

    for (int col = 0; col < m_bins_per_row; col++)
    {
        density.push_back(std::vector<float>(m_bins_per_col));
        for (int row = 0; row < m_bins_per_col; row++)
        {
            density[col][row] = m_bins[col][row].total_overlap * bin_area_inv;
        }
    }
    return density;
}

std::vector< std::vector<float> > Grid::get_a_uv()
{
    std::vector< std::vector<float> > a_uv;

    for (int col = 0; col < m_bins_per_row; col++)
    {
        a_uv.push_back(std::vector<float>(m_bins_per_col));
        for (int row = 0; row < m_bins_per_col; row++)
        {
            a_uv[col][row] = m_bins[col][row].a_uv;
        }
    }
    return a_uv;
}

float Grid::computeTotalOverflow(float target_density, float total_movable_area)
{
    float overflow_area = 0;
    for (int col = 0; col < m_bins_per_row; col++)
        for (int row = 0; row < m_bins_per_col; row++) {
            float bin_capacity = m_bins[col][row].bb.getArea() * target_density;
            float excess = m_bins[col][row].total_overlap - bin_capacity;
            overflow_area += std::max(0.0f, excess);
        }
    return overflow_area / (total_movable_area + 1e-8f);
}

/*****************
 * Print Functions
*****************/
void Grid::printOverflows()
{
    Table overflows;
    overflows.add_row(RowStream{} << "loc index" << "area" << "overlap" << "overflow");
    for( int x_index = 0; x_index < m_bins_per_row; x_index++)
    {
        for( int y_index = 0; y_index < m_bins_per_col; y_index++)
        {
            Bin bin = m_bins[x_index][y_index];
            float overflow = bin.getOverflowRatio();
            float overlap= bin.total_overlap;
            if (overflow > 0)
            {
                overflows.add_row(RowStream{} << std::to_string(x_index) + ", " + std::to_string(y_index)
                        << bin.bb.getArea() << overlap << overflow);
            }
        }
    }
    Logger::log_detail(overflows);
}

void Grid::printElectricFields()
{
    for( int x_index = 0; x_index < m_bins_per_row; x_index++)
        for( int y_index = 0; y_index < m_bins_per_col; y_index++)
        {
            Bin b = m_bins[x_index][y_index];
            cout << "Bin["<<x_index<<"]["<<y_index<<"]\tEx: " << b.eField.x << "\tEy: " << b.eField.y << endl;
        }
}
AIEPLACE_NAMESPACE_END