#ifndef AIEPLACE_H
#define AIEPLACE_H

#include "Common.h"
#include "DataBase.h"
#include "Grid.h"
#include "VersalDriver.h"

#ifdef CREATE_VISUALIZATION
    #include "Visualizer.h"
#endif
   
AIEPLACE_NAMESPACE_BEGIN

class Placer
{
public:
    DataBase db;
    Grid grid;
    VersalDriver driver;
    float gamma = 4.0; // smoothness factor for estimations; 
                       // larger means less smooth but more accurate
    int iteration = 0;
    float learning_rate = 300;
#ifdef CREATE_VISUALIZATION
    Visualizer viz;
#endif

    // Constructor
    Placer(fs::path input_dir, std::string xclbin_file) : 
        db(DataBase(input_dir)), 
        grid(Grid(db.getDieArea(), BINS_PER_ROW, BINS_PER_COL)), 
#ifdef CREATE_VISUALIZATION
        viz(Visualizer(db.getDieArea())),
#endif
        driver(VersalDriver(xclbin_file, DEVICE_ID, NUM_PIPELINES))
        { }


    static void printVersionInfo();

    // Pre-run preparation
    void initialPlacement(Position<position_type> target_pos, int min_dist, int max_dist);
    void iterationReset();

    // Functions which may be accelerated on AIEs
    void prepareInputData(float * input_data, int net_size);
    void computeAllPartials_AIE ();
    void computeElectricFields_AIE ();

    // Functions implemented on CPU
    void computeAllPartials_CPU ();
    void computeNetPartials_CPU (Net* net_p);
    void computeElectricFields_CPU ();
    void computeElectricFields_DCT();

    // CPU only computations
    void compute_a_terms_CPU (Net* net_p);
    void compute_bc_terms_CPU (Net* net_p);

    void compute_a_uv_naive();
    void compute_eField_naive();
    void compute_a_uv_DCT();
    void compute_eField_DCT();

    void computeOverlaps();

    // Run functions
    void nudgeAllNodes();
    void nudgeNode(Node*);
    void performIteration();
    void printIterationResults();
    void run();

    // Post run analysis
    void computeStatistics();
    void printRunResults();
};

AIEPLACE_NAMESPACE_END

#endif