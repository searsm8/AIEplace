#include "Common.h"
#include "Bin.h"
#include "Node.h"

AIEPLACE_NAMESPACE_BEGIN

class Grid
{
private:
    // Grid data members
    Box<position_type> m_die_area;
    int m_bins_per_row;
    int m_bins_per_col;
    std::vector<std::vector<Bin> > m_bins; // 2D grid of bins to compute eField

public:
    // Constructors
    Grid() {}

    Grid(Box<position_type> die_area) : m_die_area(die_area), m_bins_per_row(1024), m_bins_per_col(1024) { init(); }

    Grid(Box<position_type> die_area, int bins_per_row, int bins_per_col) : 
        m_die_area(die_area), m_bins_per_row(bins_per_row), m_bins_per_col(bins_per_col) { init(); }

    void init();

    // How to quickly find the Bin that a Node is in?
    Bin& getBin(int col, int row) { return m_bins[col][row]; }

    int getBinsPerRow() { return m_bins_per_row; }
    int getBinsPerCol() { return m_bins_per_col; }
    int getDieWidth() { return m_die_area.getXsize(); }
    int getDieHeight() { return m_die_area.getYsize(); }

    void iterationReset();

    void computeBinOverlaps(Node* node_p);

    std::vector< std::vector<float> > getRho();
    std::vector< std::vector<float> > get_a_uv();

    float computeTotalOverflow();

    // Print Functions
    void printOverflows();
    void printElectricFields();

};

AIEPLACE_NAMESPACE_END