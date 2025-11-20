#ifndef AIEPLACE_H
#define AIEPLACE_H

#include "Common.h"
#include "DataBase.h"
#include "Grid.h"
#include "GraphDriver.h"
#include "Logger.h" // or DebugFramework
#include "json.h"
using json = nlohmann::json;

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
    void createRunOutputStructure(std::string& run_output_dir, std::string& run_id);
    void populateStatsBlock(Logger::ProgramStatBlock& stats, 
                           float final_hpwl, float final_overflow, 
                           float total_runtime, float iteration_avg,
                           float hpwl_improvement, bool has_improvement,
                           const std::string& run_id);
    
    // Helper functions
    std::string escapeJsonString(const std::string& input);
    std::string generateRunId();
    bool checkConvergence();
    float getMemoryUsageMB();
    float getAIEUtilizationPercent();

public:
    DataBase db;
    Grid grid;
    PartialsGraphDriver partials_drivers[PARTIALS_GRAPH_COUNT];
    DensityGraphDriver density_driver[3];

    // Flat data structure for results (same as before)
    struct NodePartial {
        Node* node;
        Point partial;
    };

    std::map<Net*, NodePartial> all_partials;
    std::map<Net*, NodePartial> simple_partials;

    // Configuration object
    json cfg;

    fs::path input_dir; // parameter loaded from json config file
    fs::path output_dir;
    std::string result_csv;

    // hyper parameters
    float gamma, inv_gamma; // smoothness factor for estimations; 
                       // larger means less smooth but more accurate
    float learning_rate;
    float global_lambda;

    int die_size; // minimum of width and height of the die area
    int bins_per_row; // grid size
    int MAX_THREADS; // max number of threads to use

    std::string partials_method;
    std::string density_method;

    // Execution tracking
    int iteration = 0;
    long double pgrm_start_time;
    long double db_IO_time;
    long double algo_start;
    long double algo_time;
    std::vector<float> hpwl_history; // history of HPWL values for each iteration
    std::vector<float> ovfw_history; // history of overflow values for each iteration
    std::vector<float> learning_coeff_history; 

#ifdef CREATE_VISUALIZATION
    Visualizer viz;
#endif
    // Constructor
    Placer(std::string);

    static void printWelcomeBanner();

    // Pre-run preparation
    void initializePlacement(Position<position_type> target_pos, int min_dist, int max_dist);
    void recordInitialHPWL();
    void iterationReset();

    // Functions which may be accelerated on AIEs
    void prepareInputDataPacket(float * input_data, int net_size);
    void computeAllPartials_AIE ();
    void computePartials(Packet* p); 
    void receivePartials(Packet* p);
    void computeElectricFields_AIE ();

    // Functions implemented on CPU
    void computeAllPartials_CPU ();
    void computeAllPartials_simple();
    void computeAllPartials_CPU_orig();
    void computeNetPartials_CPU (Net* net_p);
    void computeNetPartials_ThreadSafe(Net* net_p);
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
    void updateHyperparameters();
    void nudgeNode(Node*);
    void performIteration();
    void printIterationResults();
    void plotHistories();
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