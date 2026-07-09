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

// PL port param_scheduler (scalar, cmath-only). Included by the golden so the schedule can be
// sourced from the PL module behind use_pl_scheduler -- the closed-loop drop-in check (S6 step 0).
#include "modules/param_scheduler.hpp"

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
    bool gamma_bin_scaled;  // if true, base_gamma tied to bin geometry referenced to gamma_ref_grid (grid-independent); else bare init_gamma
    float gamma_ref_grid;   // reference grid for gamma_bin_scaled: base_gamma = init_gamma*die_span/gamma_ref_grid (grid-independent absolute gamma)

    int backtrack_steps = 0;
    int max_backtracking_attempts;
    float backtrack_epsilon;
    bool enable_backtracking;
    bool enable_momentum;
    bool enable_preconditioning;
    bool precond_coef_escalation = true; // double precond_coef every 20 iters once overflow<0.3 (XPlace step_precond_coef)
    bool enable_density_clamp;   // clamp sub-bin cells in the density solve (XPlace expand_ratio)
    bool dct_normalize = true;   // apply 1/N per forward DCT (bounds a_uv intermediates; global scale absorbed by lambda)
    bool dct_normalize_inverse = true; // apply 1/N per INVERSE transform (IDCT/IDXST) in the field solve.
                                       // true = legacy: the inverse re-applies the forward's 1/N, so the
                                       // field carries an extra 1/N^2 vs the naive DREAMPlace Eq-3c/3d field
                                       // (compute_eField_naive) → density force ~N^2 too weak → lambda ~N^2
                                       // inflated (adaptec1@512 lambda_init 1.68e-5 vs XPlace 3.4e-9). The
                                       // scale is absorbed by lambda so GP is unaffected, but it mis-scales
                                       // the preconditioner's lambda*area term. false = the field-faithful
                                       // inverse (matches compute_eField_naive / DREAMPlace): lambda lands
                                       // within ~50x of XPlace instead of ~5000x. Quality-neutral (lambda-
                                       // absorbed); verify a precond-off A/B before flipping the default.
    bool compare_hpwl_methods = false;
    bool precond_raw_area = false; // preconditioner/dff area term: false = legacy area/avg_node_size,
                                   // true = raw node area (XPlace-faithful: alpha_2 = pcoef·λ·mov_node_area).
                                   // The /avg_node_size normalization was a spurious deviation — markv1
                                   // runs in the SAME raw-DBU frame as XPlace, so raw area is coordinate-
                                   // scale-invariant like XPlace's weighted_weight (area·S² and λ·1/S² cancel).
    bool dff_force_ratio = false;  // density_force_fraction basis: false = legacy area-mass a2/(a1+a2),
                                   // true = force-magnitude ‖λ·∇den‖₁ / (‖∇wl‖₁ + ‖λ·∇den‖₁). The force
                                   // ratio is INVARIANT to the field-normalization constant (field→C·field
                                   // ⇒ λ→λ/C, so λ·∇den is unchanged), making the skip_update schedule
                                   // independent of dct_normalize_inverse. Uses the previous iteration's
                                   // committed gradients (updatePrecondWeights runs before combineGradients).
    float precond_coef = 1.0f; // escalating preconditioner coefficient (doubles every 20 iters when overflow < 0.3)
    float avg_node_size = 1.0f; // average cell area; normalizes preconditioner area term
    float density_force_fraction = 0.0f; // density's share of total preconditioner force-mass, in [0,1]
                                          // (0 = all wirelength, 1 = all density); XPlace calls this "weighted_weight"
    float precond_weight_mean = 1.0f;     // mean diagonal preconditioner weight over movable nodes+fillers
                                          // (=1.0 exactly when preconditioning is off). Scales the BB
                                          // step-length upper clamp: Node::step moves by grad/precond_weight,
                                          // so as precond_weight grows (∝ λ·area late in the run) the BB
                                          // step α must grow ∝ precond_weight to keep the physical
                                          // displacement bounded; a fixed clamp starves the step and freezes
                                          // the cells (the precond-ON stall). No-op when precond is off.
    // BB-step raw sums from the last computeLipshitzEstimate (before the sqrt/clamp), exposed so the
    // schedule-trace dump can hand the PL param_scheduler port the exact inputs it must reproduce.
    float last_pos_norm_sq = 0.0f;  // ||v_{k+1} - v_k||^2 (preconditioned-consistent)
    float last_grad_norm_sq = 0.0f; // ||g(v_{k+1}) - g(v_k)||^2 (preconditioned)

    // PL param_scheduler drop-in (S6 step 0). When use_pl_scheduler, the metric-driven schedule
    // (gamma, lambda, BB alpha, convergence stop) is sourced from the PL module instead of the
    // native updateGamma/updateDensityWeight/computeLipshitzEstimate/checkConvergence, closing the
    // loop with real (CPU) gradients. momentum stays native (pure a_k recurrence). A correct drop-in
    // reproduces the native run bit-for-bit. last_g*_L1 feed the module's iteration-1 lambda init.
    bool  use_pl_scheduler = false;
    plalgo::SchedState  pl_sched_state;
    plalgo::SchedParams pl_sched_params;
    int   pl_stop = 0;
    float last_gwl_L1 = 0.0f, last_gden_L1 = 0.0f;

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
    float computeOverflow(bool clamp, std::vector<float>* out_density = nullptr);
                                        // overflow metric, fillers excluded; clamp=true = XPlace
                                        // masked (GP convergence), clamp=false = exact (physical).
                                        // out_density (optional): area deposited per bin (col*ny+row).
    void dumpBinDensity(const std::string& path_prefix); // ρ maps (masked+exact) for XPlace compare


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
    void dumpScheduleTrace(); // append this iteration's schedule I/O for the PL param_scheduler port verify

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