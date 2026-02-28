#ifndef AIEPLACE_H
#define AIEPLACE_H

#include "Common.h"
#include "DataBase.h"
#include "Grid.h"
#include "Logger.h" // or DebugFramework
#include "json.h"
using json = nlohmann::json;

#ifdef USE_XILINX_XRT
#include "GraphDriver.h"
#endif

#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>

#define DEVICE_ID 0 // Device ID to find VCK5000

#ifdef CREATE_VISUALIZATION
    #include "Visualizer.h"
#endif
   
AIEPLACE_NAMESPACE_BEGIN

class Placer
{
private:
    float initial_hpwl = 0.0f;
    
    // Helper functions for DSE integration and output organization
    void createRunOutputStructure();
    void writeResultsCSV(float final_hpwl, float final_overflow,
                         float total_runtime, float iteration_avg,
                         float hpwl_improvement, const std::string& run_id);
    
    // Helper functions
    std::string escapeJsonString(const std::string& input);
    std::string generateRunId();
    bool checkConvergence();
    float getMemoryUsageMB();
    float getAIEUtilizationPercent();

public:
    DataBase db;
    Grid grid;
#ifdef USE_XILINX_XRT
    PartialsGraphDriver partials_drivers[PARTIALS_GRAPH_COUNT];
    DensityGraphDriver density_driver[3];
#endif

    // Flat data structure for results (same as before)
    struct NodePartial {
        Node* node;
        Point partial;
    };

    std::map<Net*, NodePartial> all_partials;
    std::map<Net*, NodePartial> simple_partials;

    // Configuration object
    json cfg;
    fs::path input_dir;
    fs::path output_dir;
    fs::path results_dir;

    // Hyperparameters
    float gamma, inv_gamma; // smoothness factor for estimations;
                       // larger means less smooth but more accurate
    float step_length;
    float density_weight;

    int die_size; // minimum of width and height of the die area
    int bins_per_row; // grid size
    int MAX_THREADS; // max number of threads to use

    // Methods of computation, loaded from config file
    std::string partials_method;
    std::string density_method;

    // Convergence Criteria, loaded from config file
    int min_iterations;
    int max_iterations;
    float hpwl_improvement_threshold;
    float overflow_threshold;
    int convergence_window;
    float target_density;

    // Execution tracking
    int iteration = 0;
    long double pgrm_start_time;
    long double db_IO_time;
    long double algo_start;
    long double algo_time;
    std::vector<float> hpwl_history; // history of HPWL values for each iteration
    std::vector<float> ovfw_history; // history of overflow values for each iteration
    std::vector<float> step_length_history;

#ifdef CREATE_VISUALIZATION
    Visualizer viz;
#endif
    // Constructor
    Placer(std::string);

    static void printWelcomeBanner(bool show_info = true);

    // Getter functions
    XY getNodePartials(Node* node_p);

    // Pre-run preparation
    void initializePlacement(Position<position_type> target_pos, int min_dist, int max_dist);
    void recordInitialHPWL();
    void iterationReset();
    void initializeDensityWeight();

    // Functions to be accelerated on AIEs
    void prepareInputDataPacket(float * input_data, int net_size);
    void computeAllPartials_AIE();
    void computePartials(Packet* p); 
    void receivePartials(Packet* p);
    void computeElectricFields_AIE();

    // Functions implemented on CPU
    void computeAllPartials();
    void computeAllPartials_CPU();
    void computeAllPartials_simple();
#ifdef USE_TBB
    void computeAllPartials_CPU_orig();
#endif
    void computeNetPartials_CPU(Net* net_p);
    void computeNetPartials_ThreadSafe(Net* net_p);
    void computeElectricFields();
    void computeElectricFields_CPU();
    void computeElectricFields_DCT();
    void normalizeElectricFields();
    XY computeElectrostaticForce(Node* node_p);

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
    void updateHyperparameters();
    void updateStepLength();
    void updateDensityWeight();

    void nudgeAllNodes();
    void nudgeNode(Node*);
    void performIteration();
    void recordIterationResults();
    void printIterationResults();
    void plotHistories();
    void run();

    // Post run analysis
    void computeStatistics();

    // Print functions
    void printFinalResults();
    void initializeFocus();
};

AIEPLACE_NAMESPACE_END

#endif