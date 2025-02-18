#ifndef AIEPLACE_H
#define AIEPLACE_H

#include "Common.h"
#include "DataBase.h"
#include "Grid.h"
#include "GraphDriver.h"
#include <thread>
#include "Logger.h"
#include "json.h"
using json = nlohmann::json;

#define DEVICE_ID 0 // Device ID to find VCK5000

#ifdef CREATE_VISUALIZATION
    #include "Visualizer.h"
#endif
   
AIEPLACE_NAMESPACE_BEGIN

class Placer
{
public:
    DataBase db;
    Grid grid;
    PartialsGraphDriver partials_drivers[PARTIALS_GRAPH_COUNT];
    DensityGraphDriver density_driver[3];

    // Configuration object
    json params;

    fs::path input_dir; // parameter loaded from json config file
    fs::path output_dir;

    // hyper parameters
    float gamma; // smoothness factor for estimations; 
                       // larger means less smooth but more accurate
    float learning_rate;
    float global_lambda;
    int bins_per_row; // grid size

    // Execution tracking
    int iteration = 0;
    long double pgrm_start_time;
    long double db_IO_time;
    long double algo_start;
    long double algo_time;
    long double AIE_time;

#ifdef CREATE_VISUALIZATION
    Visualizer viz;
#endif

    // Constructor
    Placer(std::string);

    static void printWelcomeBanner();

    // Pre-run preparation
    void initializePlacement(Position<position_type> target_pos, int min_dist, int max_dist);
    void iterationReset();

    // Functions which may be accelerated on AIEs
    void prepareInputDataPacket(float * input_data, int net_size);
    void computeAllPartials_AIE ();
    void computePartials(Packet* p); 
    void receivePartials(Packet* p);
    void computeElectricFields_AIE ();

    // Functions implemented on CPU
    void computeAllPartials_CPU ();
    void computeNetPartials_CPU (Net* net_p);
    void computeElectricFields_CPU ();
    void computeElectricFields_DCT();
    void normalizeElectricFields();

    // CPU only computations
    void compute_a_terms_CPU (Net* net_p);
    void compute_bc_terms_CPU (Net* net_p);

    void compute_a_uv_naive();
    void compute_eField_naive();
    void compute_a_uv_DCT();
    void compute_eField_DCT();

    void computeOverlaps();

    // Comparison functions for verification
    void comparePartialResults();
    void compareDensityResults();

    // Run functions
    void nudgeAllNodes();
    void nudgeNode(Node*);
    void performIteration();
    void printIterationResults();
    void run();

    // Post run analysis
    void computeStatistics();

    // Timing and print functions
    long getTime();
    double getInterval(long start_time, long end_time);
    void printFinalResults();
    fs::path getOutputPath();
    void initializeFocus();
};

AIEPLACE_NAMESPACE_END

#endif