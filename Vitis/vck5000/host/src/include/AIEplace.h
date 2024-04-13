#ifndef AIEPLACE_H
#define AIEPLACE_H

#include "Common.h"
#include "DataBase.h"
#include "Grid.h"
#include "GraphDriver.h"

#define DEVICE_ID 0

#ifdef CREATE_VISUALIZATION
    #include "Visualizer.h"
#endif
   
AIEPLACE_NAMESPACE_BEGIN

class Placer
{
public:
    DataBase db;
    Grid grid;
    PartialsGraphDriver drivers[PARTIALS_GRAPH_COUNT];

    float gamma = 4.0; // smoothness factor for estimations; 
                       // larger means less smooth but more accurate
    int iteration = 0;
    float learning_rate = 300;
#ifdef CREATE_VISUALIZATION
    Visualizer viz;
#endif

    // Constructor
    Placer(fs::path input_dir, std::string xclbin_file);

    static void printVersionInfo();

    // Pre-run preparation
    void initialPlacement(Position<position_type> target_pos, int min_dist, int max_dist);
    void iterationReset();

    // Functions which may be accelerated on AIEs
    void prepareInputDataPacket(float * input_data, int net_size);
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