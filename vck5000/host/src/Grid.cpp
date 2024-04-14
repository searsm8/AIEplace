#include "Grid.h"

AIEPLACE_NAMESPACE_BEGIN

void Grid::init()
{
    float bin_width  = m_die_area.getXsize() / (float)m_bins_per_row;
    float bin_height = m_die_area.getYsize() / (float)m_bins_per_col;

    for( int x_index = 0; x_index < m_bins_per_row; x_index++)
    {
        m_bins.push_back(std::vector<Bin>());
        for( int y_index = 0; y_index < m_bins_per_col; y_index++)
        {
            Bin b = Bin(x_index*bin_width, y_index*bin_height, 
                   (x_index+1)*bin_width, (y_index+1)*bin_height);
            b.lambda = INITIAL_LAMBDA;
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

/* @brief: Compute the overlap of the given node with the grid bins
 *         After computing for all nodes, we have the density, rho. 
 *         This is the starting point for the electrostatic computation.
 * @param node_p: a pointer to the node to compute overlap.
 */
void Grid::computeBinOverlaps(Node* node_p)
{
    float bin_height = m_die_area.getYsize() / (float)m_bins_per_col;
    float bin_width  = m_die_area.getXsize() / (float)m_bins_per_row;

    // find indices in m_bins that this node overlaps
    int y_index_start = std::max(0, std::min<int>(m_bins_per_col-1, node_p->getPosition().getY() / bin_height)); 
    int x_index_start = std::max(0, std::min<int>(m_bins_per_row-1, node_p->getPosition().getX() / bin_width));

    int y_index_final = std::max(0, std::min<int>(m_bins_per_col-1, (node_p->getPosition().getY() + node_p->getYsize()) / bin_height)); 
    int x_index_final = std::max(0, std::min<int>(m_bins_per_row-1, (node_p->getPosition().getX() + node_p->getXsize()) / bin_width));

    // DEBUGGING
    //cout << "\ncomputeBinOverlaps() for Node " << node_p->getName() << node_p->getPosition().to_string() << " : " << node_p->getXsize() << ", " << node_p->getYsize() << endl;
    //cout << "bin_height: " << bin_height << "\t";
    //cout << "die_area Ysize: " << m_die_area.getYsize() << "\t";
    //cout << "y_index_start: " <<y_index_start<< "\t";
    //cout << "y_index_final: " <<y_index_final<< endl;
    //cout << "x_index_start: " <<x_index_start<< "\t";
    //cout << "x_index_final: " <<x_index_final<< endl;
    assert(y_index_start < m_bins_per_col && "y_index_start exceeds number of bins per column!");
    assert(x_index_start < m_bins_per_row && "x_index_start exceeds number of bins per row!");
    assert(y_index_final < m_bins_per_col && "y_index_final exceeds number of bins per column!");
    assert(x_index_final < m_bins_per_row && "x_index_final exceeds number of bins per row!");

    // compute overlap for the bins between start and final indices
    for (int x_index = x_index_start; x_index <= x_index_final; x_index++)
        for (int y_index = y_index_start; y_index <= y_index_final; y_index++)
        {
            m_bins[x_index][y_index].computeOverlap(node_p);
            // DEBUGGING
            //float percent = 100 * m_bins[x_index][y_index].overlap / node_p->getArea() ;
            //if (percent < 99)
            //    cout << "Bingo!\n";
            //cout << percent << "% is in m_bins["<<x_index<<"]["<<y_index<<"].overlap: " << m_bins[x_index][y_index].overlap << endl;
        }


    // TODO: Verify that overlap totals equals the area of all nodes!~


}

std::vector< std::vector<float> > Grid::getRho()
{
    std::vector< std::vector<float> > rho;

    for (int col = 0; col < m_bins_per_row; col++)
    {
        rho.push_back(std::vector<float>(m_bins_per_col));
        for (int row = 0; row < m_bins_per_col; row++)
        {
            rho[col][row] = m_bins[col][row].overlap;
        }
    }
    return rho;
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

float Grid::computeTotalOverflow()
{
    float total = 0;
    for (int col = 0; col < m_bins_per_row; col++)
        for (int row = 0; row < m_bins_per_col; row++)
            total += m_bins[col][row].computeOverflow();
    return total;
}

/*****************
 * Print Functions
*****************/
void Grid::printOverflows()
{
    for( int x_index = 0; x_index < m_bins_per_row; x_index++)
    {
        for( int y_index = 0; y_index < m_bins_per_col; y_index++)
        {
            Bin b = m_bins[x_index][y_index];
            float overflow = b.computeOverflow();
            float overlap= b.overlap;
            if (overlap > 10)
                cout << "Bin["<<x_index<<"]["<<y_index<<"] area: " << b.bb.getArea()
                    << "\toverlap: " << overlap 
                    << "\toverflow: "<< overflow<< endl;
        }
    }
}

void Grid::printElectricFields()
{
    for( int x_index = 0; x_index < m_bins_per_row; x_index++)
    {
        for( int y_index = 0; y_index < m_bins_per_col; y_index++)
        {
            Bin b = m_bins[x_index][y_index];
            cout << "Bin["<<x_index<<"]["<<y_index<<"]\tEx: " << b.eField.x << "\tEy: " << b.eField.y << endl;
        }
    }
}
AIEPLACE_NAMESPACE_END