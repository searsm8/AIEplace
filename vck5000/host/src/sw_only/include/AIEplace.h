#pragma once

#include "Common.h"
#include "DataBase.h"
#include "Grid.h"
#include "Logger.h"
#include "toml.hpp"

#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <random>

#define DEVICE_ID 0 // Device ID to find VCK5000

AIEPLACE_NAMESPACE_BEGIN

namespace ConfigUtils {
    /// @brief Required config read: logs an error and exits if [section].key is absent.
    /// toml++ returns an empty optional for a missing key rather than throwing, so the
    /// "this key is mandatory" contract has to be expressed here.
    template <typename T>
    T require(const toml::table& cfg, std::string_view section, std::string_view key)
    {
        if (auto v = cfg[section][key].value<T>())
            return *v;
        Logger::log_error("Missing required config key: [" + std::string(section) + "] " + std::string(key));
        exit(1);
    }
}

class Placer
{
private:
    float m_initial_hpwl = 0.0f;
    std::string m_config_filepath;

    // Constructor phases (see Placer::Placer)
    void setupDesign();                    // timed: config parse + grid decision + DB read + fillers + area analysis
    void loadConfiguration();              // parse the config file into cfg and read all hyperparameters
    bool resolveGridResolution();          // explicit bins_per_row override, or defer to the ePlace formula
    void loadDesignDatabase();             // read LEF/DEF, apply benchmark max_util
    void tagMovableMacros();               // XPlace is_mov_macro rule; must precede createFillers
    void createFillers();                  // fillers (may raise target_density) + flat node index
    void analyzeDesignArea(bool bins_auto); // movable/fixed area stats, macro count, ePlace-formula grid size
    void configurePreconditioner();        // auto-enable decision from num_movable_macros
    void applyMixedSizeStopPolicy();       // XPlace include_macros phase: 2x stop overflow, no plateau kill
    void setupGrid();                      // build the Grid from bins_per_row, clamp density, set die_size
    void configureGammaSchedule();         // grid-independent base_gamma, gamma/inv_gamma, LUT init
    void initializePositionDump();         // config output.dump_positions; opens generation 0 (TODO #16)

    // Helper functions for DSE integration and output organization
    void createRunOutputStructure();
    void writeResultsCSV(float final_hpwl, float final_hpwl_exact, float final_overflow,
                         float total_runtime, float iteration_avg,
                         float hpwl_improvement, const std::string& run_id);

    // writeResultsCSV's steps, broken out for readability
    float lookupXplaceReferenceHPWL(const std::string& bench_name);
    std::vector<std::pair<std::string, std::string>> parseDSEParams();
    void writeResultsCSVHeader(std::ofstream& out_file,
                               const std::vector<std::pair<std::string, std::string>>& dse_params);
    void writeResultsCSVRow(std::ofstream& out_file, float final_hpwl_exact, float total_runtime,
                            float iteration_avg,
                            const std::vector<std::pair<std::string, std::string>>& dse_params,
                            float xplace_ref);

    // Helper functions
    std::string generateRunId();
    bool checkConvergence();
    bool reachedMaxIterations();       // safety fallback: iteration >= max_iterations
    bool hasNaNMetrics();              // NaN in overflow or HPWL — hard stop
    bool hasCoarseDivergence();        // HPWL blown past 2x the best known solution
    bool checkFineDivergenceGuard();   // near-converged-band divergence guard (burns life)
    bool checkOverflowCountdown();     // XPlace-style post-threshold countdown to stop
    float getMemoryUsageMB();

public:
    DataBase db;
    Grid grid;

    // Configuration object
    toml::table cfg;
    fs::path input_dir;
    fs::path output_dir;
    fs::path results_dir;

    // Hyperparameters
    float step_length; // α (alpha) in eplace
    float init_step_seed; // BB trial-step seed for estimateInitialStep (XPlace args.lr)
    float density_weight; // λ (lambda) in eplace
    float nesterov_ak = 1.0f; // a_k in Algorithm 1; controls momentum coefficient
    float momentum_coeff; // (a_k - 1) / a_{k+1} in Algorithm 1; computed each iteration if momentum enabled
    float gamma, inv_gamma; // smoothness factor for WA gradient; updated each iteration if gamma_schedule enabled
    float base_gamma;       // reference gamma from config; schedule varies around this
    bool gamma_schedule = false;    // if true, gamma follows overflow-driven schedule (XPlace-style)
    bool gamma_bin_scaled = true;   // if true, base_gamma tied to bin geometry referenced to gamma_ref_grid (grid-independent); else bare init_gamma
    float gamma_ref_grid = 512.0f;  // reference grid for gamma_bin_scaled: base_gamma = init_gamma*die_span/gamma_ref_grid (grid-independent absolute gamma)

    int backtrack_steps = 0;
    int max_backtracking_attempts;
    float backtrack_epsilon;
    bool enable_backtracking;
    bool enable_momentum;
    bool enable_preconditioning = false;
    bool auto_enable_preconditioning = true; // if enable_preconditioning is not set explicitly, turn it
                                             // ON iff the design has movable macros (essential for MMS
                                             // convergence; a wash on fixed-macro designs). See #5 handoff.
    bool precond_explicitly_set = false;     // config named enable_preconditioning => honor it, skip auto
    int  num_movable_macros = 0;             // movable components with area > macro_area_frac * die area
    bool mixed_size_mode = false;            // macros + std cells placed together (XPlace include_macros);
                                             // set by applyMixedSizeStopPolicy, loosens the stop rules.
                                             // Cleared at the phase-2 transition.
    // TODO #13 phase 2. enable_phase2 gates the whole fixed-macro second pass; when it is off the
    // run ends at the phase-1 stop exactly as it did before phase 2 existed.
    // macro_legalization_enabled gates only STAGE 2 (overlap removal) within it — leaving it off
    // freezes the macros where GP left them, which still exercises the mechanism that produces the
    // overflow improvement and is the cheap way to test it.
    bool enable_phase2 = true;
    bool macro_legalization_enabled = true;
    std::string macro_lp_solver;             // path to the CBC binary; empty = auto-detect
    int  formula_bins_per_row = 0;           // grid the ePlace auto-formula picks (recorded even when overridden)
    bool precond_coef_escalation = true; // double precond_coef every 20 iters once overflow<0.3 (XPlace step_precond_coef)
    bool enable_density_clamp = true;   // clamp sub-bin cells in the density solve (XPlace expand_ratio)
    bool dct_normalize = true;   // apply 1/N per forward DCT (bounds a_uv intermediates; global scale absorbed by lambda)
    float precond_coef = 1.0f; // escalating preconditioner coefficient (doubles every 20 iters when overflow < 0.3)
    float avg_node_size = 1.0f; // average movable cell area; grid-sizing divisor for the no-macros case
    float density_force_fraction = 0.0f; // density's share of total preconditioner force-mass, in [0,1]
                                          // (0 = all wirelength, 1 = all density); XPlace calls this "weighted_weight"
    float precond_a1_norm = 0.0f; // ||alpha_1||_1 = sum of movable num_pins (preconditioner pin-mass; diag) [instrumentation]
    float precond_a2_norm = 0.0f; // ||alpha_2||_1 = sum of precond_coef*lambda*area (preconditioner density-mass) [instrumentation]

    // Committed wirelength/density gradient L1 norms from the previous iteration, refreshed each
    // combineGradients (seeded by initializeDensityWeight on iteration 1). Drive density_force_fraction.
    float last_gwl_L1 = 0.0f, last_gden_L1 = 0.0f;

    int die_size; // minimum of width and height of the die area
    int bins_per_row; // grid size

    // Scratch for the per-iteration reductions that must add in index order (see Common.h).
    // Members, not locals, so the buffers are allocated once for the whole run.
    OrderedReduce m_ordered_reduce;
    // Threaded gradient scatter under g_deterministic: the per-net partials are computed in
    // parallel into m_pin_partials, indexed by m_net_pin_offset[net] + pin, then replayed onto
    // the shared nodes serially in net order. Built once by buildPinPartialIndex().
    std::vector<int> m_net_pin_offset;
    std::vector<Gradient> m_pin_partials;
    void buildPinPartialIndex();

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
    int convergence_iterations = 30;        // iterations to continue after overflow < threshold
    int convergence_iterations_remaining = -1; // countdown; -1 = not yet triggered
    float target_density = 0.9f;

    // Divergence guard (XPlace need_to_early_stop / life): once the run starts climbing
    // away from its best solution, each detection burns "life"; at zero we stop and
    // restore the best placement instead of grinding through a divergent tail.
    static constexpr int MAX_LIFE = 30;
    int life = MAX_LIFE;

    // Execution tracking
    int iteration = 0;
    // Iteration at which the CURRENT phase began (XPlace ParamScheduler::init_iter,
    // param_scheduler.py:162/176, reset at every phase transition). The split is load-bearing
    // once phase 2 exists:
    //   * every SCHEDULE decision (warmup, mu decay, precond escalation, guard arming,
    //     min_iterations) reads phaseIteration(), so phase 2 gets its own warmup instead of
    //     inheriting phase 1's — XPlace reads `self.iter - self.init_iter` at every one of
    //     those sites (param_scheduler.py:285,286,300,307,351,365,393);
    //   * every REPORTED number (logs, [STOP], iterations.dat, run_summary) keeps using
    //     `iteration`, so the trace stays continuous and monotonic across the transition;
    //   * max_iterations stays ABSOLUTE — XPlace's inner_iter spans both phases (TODO #4), so
    //     it is a whole-run runaway backstop, not a per-phase budget.
    // While there is only one phase this is 0, so phaseIteration() == iteration exactly.
    int m_phase_start_iter = 0;
    int phaseIteration() const { return iteration - m_phase_start_iter; }
    // Console verbosity, resolved in loadConfiguration(). quiet wins: errors only. Otherwise
    // interactive defaults to isatty(stdout) — a terminal gets the banner and the per-iteration
    // live-status table, a pipe gets the bare minimum. The run report is written either way.
    bool quiet = false;
    bool interactive = true;
    bool m_nan_detected = false; // set when a NaN appears in the HPWL partials (hard divergence);
                             // run() breaks the loop so printFinalResults() still emits a
                             // best-so-far results row instead of the process aborting.

    // Why the run ended. Reported as a stable token so sweep runners can group results by
    // termination mode without regex-matching free-text log lines: only `converged` means the
    // overflow countdown completed; every other value marks a run that was cut short.
    enum class StopReason { RUNNING, CONVERGED, MAX_ITERATIONS, NAN_METRICS,
                            NAN_PARTIALS, DIVERGED_HPWL, DIVERGENCE_GUARD };
    StopReason m_stop_reason = StopReason::RUNNING;
    static const char* stopReasonName(StopReason reason);

    // --- Mixed-size two-phase flow (TODO #13, XPlace run_placement_nesterov.py:167-230) -------
    // Phase 1 places macros and standard cells together. Its durable output is the MACRO
    // positions: phase 2 freezes those, throws the standard-cell placement away, and re-solves
    // the standard cells against the macros as fixed obstacles. XPlace's own newblue5 goes
    // 0.1697 -> 0.0452 overflow across this boundary.
    enum class Phase { MIXED_SIZE, STDCELL_FIXED_MACRO };
    Phase m_phase = Phase::MIXED_SIZE;
    static const char* phaseName(Phase phase);

    /// @brief Take the phase-1 -> phase-2 transition if this run warrants one.
    /// @return true if phase 2 has begun and run() should keep iterating; false to end the run
    ///         (not mixed-size, already in phase 2, or phase 1 ended in a way we must not
    ///         restart from — see the body).
    bool beginFixedMacroPhase();
    void legalizeMacros();          ///< stage 2 entry point (honours macro_legalization_enabled)
    void runMacroLegalization();    ///< stage 2 proper; see MacroLegalize.cpp
    void reinitializeStdCells();    ///< XPlace randn_center: re-seed std cells, keep frozen macros
    void resetSolverState();        ///< lambda / precond / Nesterov / best-trackers / countdowns
    void reportPhaseSummary();      ///< short per-phase report emitted at each phase boundary

    // Metrics captured at the end of phase 1, so the final report can show both phases.
    struct PhaseSummary {
        bool  valid = false;
        int   iterations = 0;
        float hpwl = 0.0f;
        float overflow_smoothed = 0.0f;
        float overflow_exact = 0.0f;
        StopReason stop_reason = StopReason::RUNNING;
    };
    PhaseSummary m_phase1_summary;

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
    const BestSolution& bestReference() const; // converged best if valid, else lowest-overflow fallback
    static constexpr int BEST_SOL_MIN_ITER = 50; // don't save before this
    int last_density_jolt_iter = -1000; // tracks last emergency 2x jolt for cooldown
    

    // Legacy timing variables
    long double pgrm_start_time;
    double algo_time = 0.0;
    
    // Histories for diagnostics and visualization
    std::vector<float> hpwl_history; // history of HPWL values for each iteration
    std::vector<float> ovfw_history; // history of overflow values for each iteration
    std::vector<float> step_length_history;

    // --- Node-position dump for the offline visualizer (TODO #16) ------------------------------
    // The successor to in-loop rendering: the placer writes only what it knows -- where every node
    // is at iteration k -- and tools/generate_viz.py renders that offline, at any zoom, any window,
    // any number of times, without re-running a placement that took an hour. The cairo renderer
    // (Visualizer.{h,cpp}, CREATE_VISUALIZATION) was deleted in TODO #16 step 5; this dump path
    // was always independent of it, so removal needed no change here.
    //
    // Gated by output.dump_positions, default OFF -- these files are large (~96 MB per adaptec1
    // run, ~480 MB per bigblue4) and no run should pay for them unasked. Cadence is
    // output.iterations_per_dump (default 20), separate from the renderer's own cadence key.
    // Format spec: 1_REVIEW/handoffs/NEW_HANDOFF_viz_offline_tool_20260805.md §4.
    //
    // A "generation" is one span of the run over which the node SET is constant. Phase 2 breaks
    // that twice -- freezeMovableMacros() moves macros out of the movable list, rebuildFillers()
    // replaces the fillers -- so each break closes the current generation and opens a new one with
    // its own static-node file. Without this the frames and the static records silently desync and
    // every phase-2 frame renders as garbage.
    struct DumpGeneration {
        int id = 0;
        std::string phase;
        int first_iter = 0;
        int num_static_nodes = 0;  // records in nodes_gen<N>.bin (movable+filler, then fixed, then IO)
        int frame_nodes = 0;       // positions per frame = the movable+filler prefix of that order
        int filler_start = 0;      // index within the prefix where the fillers begin
        long long clamped = 0;     // positions that hit the quantization clamp (see dumpIterationPositions)
        std::vector<int> frame_iters;
        std::vector<std::string> frame_tags;  // "" or a boundary caption, parallel to frame_iters
    };
    struct PositionDump {
        bool enabled = false;
        int  interval = 20;
        fs::path dir;
        std::ofstream frames;      // frames_gen<N>.bin for the generation currently open
        std::vector<DumpGeneration> generations;
        float qx0 = 0.0f, qy0 = 0.0f, qw = 1.0f, qh = 1.0f;  // uint16 quantization box
    };
    PositionDump m_pos_dump;

    void beginPositionDumpGeneration();   ///< close the open generation, open the next one
    void dumpIterationPositions(const std::string& tag = "");  ///< one frame; forced when tagged
    void finalizePositionDump();          ///< close the stream and write manifest.json

    // Constructor
    Placer(std::string);

    void printWelcomeBanner(bool show_info = true);

    // Pre-run preparation
    void initializePlacement();
    void recordInitialHPWL();
    void iterationReset();
    void initializeDensityWeight();

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
    float computeOverflow(bool smooth, std::vector<float>* out_density = nullptr,
                          bool include_fillers = false, bool exclude_macros = false);
                                        // overflow metric; smooth=true = smoothed (GP convergence,
                                        // XPlace expand_ratio field), smooth=false = exact (physical).
                                        // include_fillers=true mirrors XPlace's filler-inclusive GP
                                        // stop signal (diagnostic only; default excludes fillers).
                                        // exclude_macros=true mirrors XPlace's zero_macro_grad (the
                                        // Mixed-GP phase-1 reference number): movable macros are
                                        // dropped from the deposit, denominator unchanged.
                                        // out_density (optional): area deposited per bin (col*ny+row).
    void dumpBinDensity(const std::string& path_prefix); // ρ maps (smoothed+exact) for XPlace compare

    // Main algorithm loop functions
    void run();
    void performIteration();

    // Main algorithm iteration functions
    void performIterationZero();        // bootstrap gradients + solver state, before iteration 1
    void combineGradients();            // subtract electro from probe_grad in-place
    float computeLipschitzEstimate();    // BB step estimate: ||Δv|| / ||Δ∇f||
    void estimateInitialStep();         // XPlace-style iteration-1 BB learning-rate estimate
    void performNextStep(bool backtracking_enabled = true); // Algorithm 2: BkTrk
    void advanceIterationState();       // promote next → current for all nodes
    void stepAllNodes();                // Algorithm 1, lines 2–4
    void enforceDieBoundaries(Node* node_p);           // clamp next.node_pos to die area
    void updateSchedule();              // throttled γ/λ update (skip_update gate)
    void updateDensityWeight();
    void updateGamma(float overflow);
    void updatePrecondWeights();
    bool checkOverflowPlateau(int window, float threshold);
    bool checkDivergence(int window, float threshold);

    // Diagnostics
    void logStepDiagnostics();

    // Bookkeeping and visualization
    bool convergenceIncludesFillers();   // config params.convergence_include_fillers
    void recordIterationResults();
    void snapshotBestPlacement();
    void restoreBestPlacement();

    // Post run analysis
    void computeStatistics();

    // Print functions
    void printIterationResults();
    void printFinalResults();

    // printIterationResults's steps, broken out for readability
    void printDSEInfoTable();                             // config output.DSE_info
    void printIterationSummaryTable(float hpwl, float overflow);
    void appendIterationLog(float hpwl, float overflow);  // iterations.dat

    // printFinalResults's steps, broken out for readability
    struct FinalMetrics {
        float final_hpwl, final_hpwl_exact;
        float final_overflow, final_smoothed_overflow;
        float final_overflow_macro_excluded;  // sharp/no-filler, movable macros dropped from the
                                               // deposit; the number comparable to XPlace's
                                               // Mixed-GP reference (_XPLACE_MMS_MIXED_GP).
        float total_runtime, iteration_avg;
        float hpwl_improvement;
        bool has_improvement;
    };
    BestSolution& restoreBestSolution(); // primary (converged) > fallback (Pareto) > last; restores its placement
    FinalMetrics computeFinalMetrics();
    void logOverflowDiagnostics();
    void dumpBestPlacementDensity();
    void exportSummaryReports(const BestSolution& chosen, const FinalMetrics& metrics,
                              const std::string& run_output_dir);
    void writeFinalDesignArtifacts(const std::string& run_output_dir);
};

AIEPLACE_NAMESPACE_END

