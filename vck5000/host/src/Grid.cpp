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
    // Let's simplfy
    // Instead of overlaps, let's just assume a node is entirely within a single bin
    // If the node is a large macro on the same order as bin size, perhaps treat it differently
    // But small nodes should be simpilified!!!!

    //If node is small compared to bin size, we assume the entire node is inside the bin
    if(!node_p->isLarge()) {
        int col_index = node_p->getPosition().getX() / m_bin_width;
        if(col_index < 0 || col_index > m_bins_per_row-1) return;
        int row_index = node_p->getPosition().getY() / m_bin_height; 
        if(row_index < 0 || row_index > m_bins_per_col-1) return;

        float overlap = node_p->getArea();
        m_bins[col_index][row_index].overlap += overlap;
        m_bins[col_index][row_index].overlapping_nodes.push_back(node_p);
        node_p->addBinOverlap(&m_bins[col_index][row_index], overlap);
        return;
    }
    else log_error("Large node found: " + node_p->getName()); //TODO: handle large nodes!

    // find indices in m_bins that this node overlaps
    int col_index_start = node_p->getPosition().getX() / m_bin_width;
    int col_index_final = std::min<int>(m_bins_per_row-1, (node_p->getPosition().getX() + node_p->getXsize()) / m_bin_width);

    int row_index_start = node_p->getPosition().getY() / m_bin_height; 
    int row_index_final = std::min<int>(m_bins_per_col-1, (node_p->getPosition().getY() + node_p->getYsize()) / m_bin_height); 

    // DEBUGGING
    //cout << "\ncomputeBinOverlaps() for Node " << node_p->getName() << node_p->getPosition().to_string() << " : " << node_p->getXsize() << ", " << node_p->getYsize() << endl;
    //cout << "bin_height: " << bin_height << "\t";
    //cout << "die_area Ysize: " << m_die_area.getYsize() << "\t";
    //cout << "y_index_start: " <<y_index_start<< "\t";
    //cout << "y_index_final: " <<y_index_final<< endl;
    //cout << "x_index_start: " <<x_index_start<< "\t";
    //cout << "x_index_final: " <<x_index_final<< endl;
    //assert(row_index_start < m_bins_per_col && "y_index_start exceeds number of bins per column!");
    //assert(row_index_final < m_bins_per_col && "y_index_final exceeds number of bins per column!");
    //assert(col_index_start < m_bins_per_row && "x_index_start exceeds number of bins per row!");
    //assert(col_index_final < m_bins_per_row && "x_index_final exceeds number of bins per row!");

    // check for out of bounds nodes
    if (col_index_start < 0 || row_index_start < 0) return;

    // compute overlap for the bins between start and final indices
    for (int col_index = col_index_start; col_index <= col_index_final; col_index++)
        for (int row_index = row_index_start; row_index <= row_index_final; row_index++)
        {
            //m_bins[row_index][col_index].computeOverlap(node_p);
            float old_overlap = m_bins[col_index][row_index].overlap;
            m_bins[col_index][row_index].computeOverlap(node_p); // dangerous, relies on indices being correct

            //DEBUG
            float delta_overlap = m_bins[col_index][row_index].overlap - old_overlap;
            if (delta_overlap < 0) {
                cout << "Negative overlap:\tcol_index: " << col_index<< "\trow_index: " << row_index << endl;
                cout << "col_index_start: " << col_index_start << "\tcol_index_final: " << col_index_final << endl;
                cout << "row_index_start: " << row_index_start << "\trow_index_final: " << row_index_final << endl;

                cout << "Overlap for node " << node_p->getName() << ": " << endl;
                for (auto bo : node_p->getBinOverlaps())
                {
                    cout << "Bin: Bot Left: " << bo.bin->bb.getPosBottomLeft().to_string() << " Top Right: " << bo.bin->bb.getPosTopRight().to_string() << " " << bo.overlap << endl;
                }
            }


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
            // Do I need to divide by area?
            //rho[col][row] = m_bins[col][row].overlap / m_bins[col][row].bb.getArea();
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
    Table overflows;
    overflows.add_row(RowStream{} << "loc index" << "area" << "overlap" << "overflow");
    for( int x_index = 0; x_index < m_bins_per_row; x_index++)
    {
        for( int y_index = 0; y_index < m_bins_per_col; y_index++)
        {
            Bin bin = m_bins[x_index][y_index];
            float overflow = bin.computeOverflow();
            float overlap= bin.overlap;
            if (overlap > 10) 
            //if (overflow > 0)
            {
                overflows.add_row(RowStream{} << std::to_string(x_index) + ", " + std::to_string(y_index)
                        << bin.bb.getArea() << overlap << overflow);
            }
        }
    }
    log_info(overflows);
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