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
    float m_initial_hpwl = 0.0f;
    std::string m_config_filepath;
    
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

    // Configuration object
    json cfg;
    fs::path input_dir;
    fs::path output_dir;
    fs::path results_dir;

    // Hyperparameters
    float step_length; // α (alpha) in eplace
    float density_weight; // λ (lambda) in eplace
    float nesterov_ak = 1.0f; // a_k in Algorithm 1; controls momentum coefficient
    float momentum_coeff; // (a_k - 1) / a_{k+1} in Algorithm 1; computed each iteration if momentum enabled
    float gamma, inv_gamma; // smoothness factor for WA gradient; updated each iteration if gamma_schedule enabled
    float base_gamma;       // reference gamma from config; schedule varies around this
    bool gamma_schedule;    // if true, gamma follows overflow-driven schedule (XPlace-style)

    int backtrack_steps = 0;
    int max_backtracking_attempts;
    float backtrack_epsilon;
    bool enable_backtracking;
    bool enable_momentum;
    bool enable_preconditioning;
    bool enable_density_clamp;   // clamp sub-bin cells in the density solve (XPlace expand_ratio)
    bool compare_hpwl_methods = false;
    float precond_coef = 1.0f; // escalating preconditioner coefficient (doubles every 20 iters when overflow < 0.3)
    float avg_node_size = 1.0f; // average cell area; normalizes preconditioner area term
    float density_force_fraction = 0.0f; // density's share of total preconditioner force-mass, in [0,1]
                                          // (0 = all wirelength, 1 = all density); XPlace calls this "weighted_weight"

    int die_size; // minimum of width and height of the die area
    int bins_per_row; // grid size
    int MAX_THREADS; // max number of threads to use

    // Methods of computation, loaded from config file
    std::string partials_method;
    std::string density_method;

    // LUT for simplified HPWL gradient (used by computeHpwlPartials_simple)
    // Stores exp(-x) for normalized x = d/gamma, x in [0, LUT_GAMMA_MULTIPLIER].
    // LUT is built once; only inv_lut_step and hpwl_lut_range are updated when gamma changes.
    static constexpr float LUT_STEP_NORM = 0.1f;   // step in normalized units (d/gamma)
    static constexpr int LUT_GAMMA_MULTIPLIER = 5;  // max normalized distance = 5γ
    float hpwl_lut_range = 0.0f;  // = LUT_GAMMA_MULTIPLIER * gamma (physical units cutoff)
    int hpwl_lut_size = 0;
    float inv_lut_step = 0.0f;    // = 1 / (LUT_STEP_NORM * gamma); updated with gamma
    std::vector<float> hpwl_lut;

    // Convergence Criteria, loaded from config file
    int min_iterations;
    int max_iterations;
    float hpwl_improvement_threshold;
    float overflow_threshold;
    int convergence_window;
    int convergence_iterations;             // iterations to continue after overflow < threshold
    int convergence_iterations_remaining = -1; // countdown; -1 = not yet triggered
    float target_density;

    // Divergence guard (XPlace need_to_early_stop / life): once the run starts climbing
    // away from its best solution, each detection burns "life"; at zero we stop and
    // restore the best placement instead of grinding through a divergent tail.
    static constexpr int MAX_LIFE = 30;
    int life = MAX_LIFE;

    // Execution tracking
    int iteration = 0;
    bool quiet; // if true, suppress all console output except errors (for DSE runs)

    // Two-tier best solution tracking (XPlace-inspired):
    //   Primary: lowest HPWL among solutions with overflow < convergence threshold
    //   Fallback: Pareto-improving (overflow strictly decreasing, HPWL within 1%)
    struct BestSolution {
        float hpwl = std::numeric_limits<float>::max();
        float overflow = std::numeric_limits<float>::max();
        int iteration = 0;
        bool valid = false;
    };
    BestSolution best_primary;   // HPWL-driven, only when overflow < threshold
    BestSolution best_fallback;  // Pareto-improving, always available
    static constexpr int BEST_SOL_MIN_ITER = 50; // don't save before this
    int last_density_jolt_iter = -1000; // tracks last emergency 2x jolt for cooldown
    

    // Legacy timing variables
    long double pgrm_start_time;
    long double db_IO_time;
    double algo_time = 0.0;
    
    // Histories for diagnostics and visualization
    std::vector<float> hpwl_history; // history of HPWL values for each iteration
    std::vector<float> ovfw_history; // history of overflow values for each iteration
    std::vector<float> step_length_history;

#ifdef CREATE_VISUALIZATION
    Visualizer viz;
#endif
    // Constructor
    Placer(std::string);

    void printWelcomeBanner(bool show_info = true);

    // Pre-run preparation
    void initializePlacement(Position target_pos, int min_dist, int max_dist);
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
    void computeHpwlPartials();
    void computeHpwlPartials_CPU();
    void computeHpwlPartials_simple();
    void initHpwlLut();
    inline float lutLookup(float d) const;
    void computeElectricFields();
    void computeElectricFields_CPU();
    void computeElectricFields_DCT();
    Gradient computeElectrostaticForce(Node* node_p);

    void compute_a_uv_naive();
    void compute_eField_naive();
    void compute_a_uv_DCT();
    void compute_eField_DCT();

    void computeOverlaps();
    float computeOverflow(bool clamp);  // overflow metric, fillers excluded; clamp=true = XPlace
                                        // masked (GP convergence), clamp=false = exact (physical)


    // Comparison functions for verification
    void compareDensityResults();
    void compareHpwlPartials();

    // Main algorithm loop functions
    void run();
    void performIteration();

    // Main algorithm iteration functions
    void combineGradients();            // subtract electro from probe_grad in-place
    float computeLipshitzEstimate();    // BB step estimate: ||Δv|| / ||Δ∇f||
    void performNextStep(bool backtracking_enabled = true); // Algorithm 2: BkTrk
    void advanceIterationState();       // promote next → current for all nodes
    void stepAllNodes();                // Algorithm 1, lines 2–4
    void enforceDieBoundaries(Node* node_p);           // clamp next.node_pos to die area
    void updateDensityWeight();
    void updateGamma(float overflow);
    void updatePrecondWeights();
    bool checkOverflowPlateau(int window, float threshold);
    bool checkDivergence(int window, float threshold);

    // Diagnostics
    void logStepDiagnostics();

    // Bookkeeping and visualization
    void recordIterationResults();
    void snapshotBestPlacement();
    void restoreBestPlacement();
    void plotHistories();

    // Post run analysis
    void computeStatistics();

    // Print functions
    void printIterationResults();
    void printFinalResults();
    void initializeFocus();
};

AIEPLACE_NAMESPACE_END

#endif