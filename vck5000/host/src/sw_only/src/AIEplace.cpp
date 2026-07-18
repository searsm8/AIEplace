#include "DCT.h"
#include "AIEplace.h"
#include "JsonUtils.h"
#include <cmath>
#include <cassert>
#include <numeric>
#include <random>

AIEPLACE_NAMESPACE_BEGIN

/**
 * @brief Run the ePlace algorithm
 *
 * Performs iterations until the convergence condition is met.
 * Each iteration consists of updating hyperparameters, calling performIteration(),
 * and checking convergence.
 */
void Placer::run()
{
    TIME_FUNCTION();

    // Set the center point of die area as initial placement target
    Position target = Position(grid.getDieWidth()/2, grid.getDieHeight()/2);

    // read init_spread parameter
    float init_spread = cfg["params"].value("init_spread", 0.25f);
    int max_dist = (int)(std::min(grid.getDieWidth(), grid.getDieHeight()) * init_spread);

    initializePlacement(target, 0, max_dist);

    bool converged = false;
    while( !converged )
    {
        performIteration();
        // Convergence from the PL param_scheduler stop flag (set in performIteration) when the
        // drop-in is active; else the native checkConvergence. (S6 step 0 closed-loop check.)
        converged = use_pl_scheduler ? (pl_stop != 0) : checkConvergence();
    }

}

void Placer::performIteration()
{
    Logger::log_detail("BEGIN iteration " + std::to_string(iteration));
    ++iteration;

    // Iteration 1: compute first gradients and initialize solver state.
    // Probe positions (v_1 = u_1) are already set by initializePlacement().
    if (iteration == 1)
    {
        iterationReset();

        computeHpwlPartials();      // ∇HPWL at probe positions → next.probe_grad (HPWL-only)
        if (compare_hpwl_methods) compareHpwlPartials();
        computeElectricFields();    // ∇D from ρ → bin eFields

        initializeDensityWeight();
        recordInitialHPWL();
    }

    // Update per-node preconditioner weights (fixed for this iteration, unaffected by backtracking)
    updatePrecondWeights();

    // Algorithm 1 + Algorithm 2: step with backtracking line search.
    // On exit: positions at u_{k+1}, v_{k+1}; gradients at v_{k+1} already computed;
    // nesterov_ak advanced.
    performNextStep(enable_backtracking);

    recordIterationResults();
    printIterationResults();

    if (use_pl_scheduler) {
        // Source the metric-driven schedule from the PL param_scheduler module instead of the native
        // updateGamma/updateDensityWeight/computeLipshitzEstimate-clamp/checkConvergence, closing the
        // loop with real CPU gradients. dff is the density_force_fraction updatePrecondWeights just
        // set; the BB norms are what computeLipshitzEstimate stored this iteration. Momentum stays
        // native. A correct drop-in reproduces the native run bit-for-bit.
        float ig, al, co, la; int stop;
        plalgo::param_scheduler(pl_sched_state, pl_sched_params,
                                hpwl_history.back(), ovfw_history.back(),
                                last_pos_norm_sq, last_grad_norm_sq, density_force_fraction,
                                last_gwl_L1, last_gden_L1, ig, al, co, la, stop);
        inv_gamma      = ig;
        gamma          = 1.0f / ig;
        step_length    = al;
        density_weight = la;
        pl_stop        = stop;
    } else {
        // Xplace param_scheduler.step(): one shared skip_update flag throttles the density-weight,
        // gamma (wa_coeff), and precond-coef updates together — freezing all three on 2 of every 3
        // iterations during the early stage (iter<50) or while the wirelength/density forces are
        // mid-balance (density_force_fraction ∈ (0.5,0.95)). sw_only previously scoped this gate to
        // density_weight only, letting gamma sharpen 3x too fast early and over-clumping the cells.
        bool skip_update = ((iteration < 50) ||
                            (density_force_fraction > 0.5f && density_force_fraction < 0.95f))
                           && (iteration % 3 != 0);
        if (!skip_update) {
            if (gamma_schedule)
                updateGamma(ovfw_history.back());
            updateDensityWeight();
        }
    }

    // After the γ/λ updates: the scalars now hold the values the NEXT iteration will consume.
    // Dump them (with the inputs that produced them) so the PL param_scheduler port can be
    // verified offline, iteration-by-iteration, against this golden trace. Gated by config.
    if (cfg["output"].value("dump_schedule_trace", false))
        dumpScheduleTrace();
}


/**
 * @brief Constructor. initialize a new Placer object and the placement system
 * Initialize a new Placer object by reading JSON configuration file.
 * Initializes the database from LEF/DEF files. (See DataBase.cpp constructor)
 * Initialize the grid structure based on size of benchmark.
 * If hardware acceleration is enabled, initializes XRT/AIE drivers.
 *
 * @param config_filepath Path to the JSON configuration file
 */
Placer::Placer(std::string config_filepath) 
        { 
            m_config_filepath = config_filepath;
            // Read configuration file (supports JSON with comments)
            std::ifstream config_file(config_filepath);
            // check if config file was found
            if (!config_file.is_open()) {
                Logger::log_error("Unable to open configuration file: " + config_filepath);
                exit(1);
            }

            pgrm_start_time = getTime();

            // Read file content and strip comments
            std::stringstream buffer;
            buffer << config_file.rdbuf();
            config_file.close();
            std::string config_content = buffer.str();
            std::string json_content = JsonUtils::stripComments(config_content);

            // Parse JSON
            cfg = json::parse(json_content);

            // Setup logging (quiet mode suppresses all output except errors)
            quiet = cfg["output"].value("quiet", false);
            Logger::setup_logging(quiet);
            printWelcomeBanner();
            Logger::log_info("Reading runtime configuration from: " + config_filepath);

            // Read hyperparameters. gamma/base_gamma are finalized after the grid is built
            // (gamma_bin_scaled ties base_gamma to the bin geometry — see after grid creation).
            base_gamma    = cfg["params"]["init_gamma"];
            gamma_schedule = cfg["params"].value("gamma_schedule", false);
            gamma_bin_scaled = cfg["params"].value("gamma_bin_scaled", true);
            gamma_ref_grid   = cfg["params"].value("gamma_ref_grid", 512.0f);
            step_length = cfg["params"]["init_step_length"];
            density_weight = 1.0f; // will be updated on iteration 1 after computing gradients

            // Read compute methods
            partials_method = cfg["params"]["partials_compute_method"];
            density_method = cfg["params"]["density_compute_method"];
            Logger::log_info("Partials compute method: " + partials_method);
            Logger::log_info("Density compute method:  " + density_method);

            // Read Convergence criteria
            max_iterations = cfg["params"]["convergence_max_iterations"];
            min_iterations = cfg["params"]["convergence_min_iterations"];
            hpwl_improvement_threshold = cfg["params"]["convergence_hpwl_improvement_threshold"];
            overflow_threshold = cfg["params"]["convergence_overflow_threshold"];
            target_density = cfg["params"].value("maximum_utilization", 0.9f);
            enable_backtracking = cfg["params"]["enable_backtracking"];
            enable_momentum = cfg["params"]["enable_momentum"];
            precond_explicitly_set = cfg["params"].contains("enable_preconditioning");
            enable_preconditioning = cfg["params"].value("enable_preconditioning", false);
            auto_enable_preconditioning = cfg["params"].value("auto_enable_preconditioning", true);
            precond_coef_escalation = cfg["params"].value("precond_coef_escalation", true);
            enable_density_clamp = cfg["params"].value("enable_density_clamp", true);
            dct_normalize = cfg["params"].value("dct_normalize", true);
            // XPlace/DREAMPlace-faithful field frame (2026-07-12 A/B: -1.0% adaptec1@512,
            // -3.0% adaptec2@1024). dff_force_ratio=true is REQUIRED with the faithful inverse.
            dct_normalize_inverse = cfg["params"].value("dct_normalize_inverse", false);
            precond_raw_area = cfg["params"].value("precond_raw_area", true);
            dff_force_ratio  = cfg["params"].value("dff_force_ratio", true);
            // EXPERIMENT: scale the preconditioner density-mass term (alpha_2 = pcoef*lambda*area) only,
            // WITHOUT touching the main force (which is lambda*E, already XPlace-matched). sw_only's field
            // E is ~50x larger than DREAMPlace's => lambda ~50x smaller => alpha_2 ~50x under-weighted vs
            // num_pins, so the preconditioner never enters XPlace's area-dominated regime. Set this to the
            // measured field-norm ratio (~50 on adaptec1) to match XPlace's a1/a2 basis and test whether a
            // basis-matched preconditioner actually helps. Default 1.0 = unchanged.
            precond_density_scale = cfg["params"].value("precond_density_scale", 1.0f);
            convergence_window = cfg["params"]["convergence_window"];
            convergence_iterations = cfg["params"].value("convergence_iterations", 30);
            max_backtracking_attempts = cfg["params"]["backtrack_max_tries"];
            backtrack_epsilon = cfg["params"]["backtrack_epsilon"];

            // Read other stuff
            compare_hpwl_methods = cfg["output"].value("compare_hpwl_methods", false);
            MAX_THREADS = cfg["params"]["max_threads"];
            input_dir = fs::path(cfg["input"]["benchmark"]);
            results_dir = fs::path(cfg["output"]["results_dir"].get<std::string>());

            // Grid resolution. An explicit config `bins_per_row` is an ad-hoc override (kept for
            // per-benchmark tuning / A/B); otherwise the grid is auto-sized by the ePlace formula
            // once the die/cell areas are known (see below, after the DB is read). The AIE datapath
            // is a fixed-size pipeline, so it always uses the compile-time BINS_PER_ROW grid.
            bool bins_auto = false;
            if (cfg["params"].contains("bins_per_row")) {
                bins_per_row = cfg["params"]["bins_per_row"];
            } else if (density_method == "aie") {
                bins_per_row = BINS_PER_ROW;
            } else {
                bins_auto = true;
            }
            if (density_method == "aie" && bins_per_row != BINS_PER_ROW) {
                Logger::log_error("bins_per_row=" + std::to_string(bins_per_row)
                    + " but density_method='aie' requires bins_per_row=" + std::to_string(BINS_PER_ROW)
                    + " (hardware constraint)");
                exit(1);
            }
            if (!bins_auto)
                Logger::log_info("Grid resolution: " + std::to_string(bins_per_row) + " x " + std::to_string(bins_per_row));

// AI Summary:
// The following section initializes the Xilinx Runtime (XRT) and AI Engine (AIE) drivers if hardware acceleration
// is requested in the config file. It checks the specified compute methods for partials and density, 
// and if either is set to "aie", it attempts to initialize the XRT device and load the xclbin file 
// containing the AIE graph. If XRT support is not compiled in but AIE acceleration is requested, 
// it logs an error and exits.
#ifdef USE_XILINX_XRT
            string xclbin_file = cfg["input"]["xclbin"];
            if(partials_method == "aie" || density_method == "aie") {
                TIME_BLOCK("AIE setup");
                // Open Xilinx Device
                xrt::device device = xrt::device(DEVICE_ID);
                Logger::log_info("Device found -- ID: " + std::to_string(DEVICE_ID));

                // Load xclbin which includes PL and AIE graph
                Logger::log_info("Loading xclbin: \"" + xclbin_file + "\"");
                xrt::uuid xclbin_uuid = device.load_xclbin(xclbin_file);
                Logger::log_info("Success!");

                if(partials_method == "aie") {
                    // Create drivers which handle buffer IO
                    for(int i = 0; i < PARTIALS_GRAPH_COUNT; i++)
                        partials_drivers[i].init(device, xclbin_uuid, i);
                }

                if(density_method == "aie") {
                    density_driver[0].init(device, xclbin_uuid, 0, BINS_PER_ROW); // DCT graph
                    density_driver[1].init(device, xclbin_uuid, 1, BINS_PER_ROW); // IDCT graph
                    density_driver[2].init(device, xclbin_uuid, 2, BINS_PER_ROW); // IDXST graph
                }
            }
#else
            if(partials_method == "aie" || density_method == "aie") {
                Logger::log_error("AIE acceleration requested but not compiled with XRT support!");
                Logger::log_error("Recompile with XILINX_XRT environment variable set, or use CPU methods.");
                exit(1);
            }
#endif

            // Initialize database by reading LEF and DEF design files
            bool enable_pin_offsets = cfg["params"].value("enable_pin_offsets", true);
            Logger::log_info(std::string("Pin offsets: ") + (enable_pin_offsets ? "enabled" : "disabled"));
            db = DataBase(input_dir, enable_pin_offsets); // TODO: Database initialization should be multithreaded?

            // Benchmark-specified maximum_utilization overrides config default
            if (db.getMaximumUtilization() > 0.0f) {
                target_density = db.getMaximumUtilization();
                Logger::log_info("Using benchmark maximum_utilization: " +
                                std::to_string(target_density));
            }

            if(cfg["params"]["enable_filler"])
                db.addFillers(target_density);

            // Preconditioner area normalization: average cell area so area term is O(1) for standard cells
            // (XPlace achieves this by normalizing all coordinates by site_width)
            // Preconditioner normalization: use movable area and count only
            {
                int movable_count = 0;
                float movable_height_sum = 0.0f, fixed_area = 0.0f;
                // die-relative macro threshold, same 0.02% rule the Visualizer uses to color macros red
                double macro_area_thresh = 0.0002 * db.getDieArea().getArea();
                for (auto& item : db.getComponents())
                    if (item.second->getStatus() != FIXED) {
                        movable_count++;
                        movable_height_sum += item.second->getYsize();
                        if ((double)item.second->getXsize() * item.second->getYsize() > macro_area_thresh)
                            num_movable_macros++;
                    } else {
                        fixed_area += item.second->getXsize() * item.second->getYsize();
                    }
                avg_node_size = db.getTotalMovableArea() / std::max(1, movable_count);

                // ePlace grid sizing (paper: |B| = V_R * target_density / (k * avg_cell_area), k=1 =>
                // ~one movable cell per bin). V_R is the placement region = die minus fixed blocks;
                // with target_density = util = movable_area/V_R this gives |B| ~ N_movable. Round to a
                // power of 2 (FFT needs it), then cap so a bin is never shorter than a standard-cell row
                // (bin_height >= row_height), mirroring XPlace's num_bin <= num_rows guard. Only runs
                // when the grid was not pinned by config or by the fixed AIE datapath.
                if (bins_auto) {
                    float placeable_area = std::max(1.0f, db.getDieArea().getArea() - fixed_area);
                    float total_bins = placeable_area * target_density / std::max(1.0f, avg_node_size);
                    int   bins       = 1 << std::clamp((int)std::lround(std::log2(std::sqrt(total_bins))), 3, 12);
                    float row_height = movable_height_sum / std::max(1, movable_count);
                    int   num_rows   = (int)(db.getDieArea().getYsize() / std::max(1.0f, row_height));
                    int   row_cap    = 1 << std::clamp((int)std::floor(std::log2((float)std::max(1, num_rows))), 3, 12);
                    bins_per_row = std::min(bins, row_cap);
                    Logger::log_info("Grid resolution (ePlace auto): " + std::to_string(bins_per_row)
                        + " x " + std::to_string(bins_per_row) + "  [sqrt|B|=" + std::to_string(bins)
                        + ", num_rows=" + std::to_string(num_rows) + ", row_cap=" + std::to_string(row_cap) + "]");
                }
            }

            // Smart default: the preconditioner is essential for movable-macro (MMS) convergence but a
            // wash on fixed-macro designs. When the config did not name enable_preconditioning, turn it
            // ON iff this design has movable macros. An explicit config value always wins.
            if (auto_enable_preconditioning && !precond_explicitly_set) {
                enable_preconditioning = (num_movable_macros > 0);
                Logger::log_info("Preconditioner auto-" + std::string(enable_preconditioning ? "ON" : "OFF")
                    + " (" + std::to_string(num_movable_macros) + " movable macros detected)");
            } else {
                Logger::log_info("Movable macros detected: " + std::to_string(num_movable_macros)
                    + " (preconditioner " + std::string(enable_preconditioning ? "ON" : "OFF") + ", explicit)");
            }

            db_IO_time = getInterval(pgrm_start_time, getTime());
            Logger::log_info("db read time: " + std::to_string(db_IO_time));

            // Create organized output directory with timestamp and method names
            // Must be after database initialization to get benchmark name
            createRunOutputStructure();

            grid = Grid(db.getDieArea(), bins_per_row, bins_per_row);
            grid.setClampDensity(enable_density_clamp);

            // Finalize the WA smoothing length. gamma is a physical length (the softmax temperature
            // of the WA HPWL surrogate); the optimal ABSOLUTE gamma tracks the layout, not the bin
            // count. XPlace ties base_gamma to bin size (base_gamma = wa_coeff*(unit_len_x+unit_len_y),
            // param_scheduler.py) which is proportional to 1/N; that over-sharpens at fine grids for
            // gamma-sensitive designs (adaptec2@1024 lost ~12% HPWL vs @512). We instead reference the
            // bin geometry to a FIXED grid (gamma_ref_grid, default 512) so base_gamma is
            // grid-INDEPENDENT: base_gamma = init_gamma*(die_w+die_h)/gamma_ref_grid. At the reference
            // grid this equals the bin-tied form exactly (bin_w+bin_h == die_span/gamma_ref_grid), so
            // the tuned @512 suite is unchanged; at other resolutions the absolute gamma is preserved.
            // sw_only's init_gamma plays XPlace's wa_coeff role. gamma_bin_scaled=false = legacy bare
            // constant. gamma_schedule starts gamma at 10x base (overflow~1) and shrinks it as overflow
            // drops (updateGamma).
            if (gamma_bin_scaled)
                base_gamma = base_gamma * (grid.getDieWidth() + grid.getDieHeight()) / gamma_ref_grid;
            gamma     = gamma_schedule ? 10.0f * base_gamma : base_gamma;
            inv_gamma = 1.0f / gamma;
            if (partials_method == "simple")
                initHpwlLut();
            Logger::log_info("WA gamma: base_gamma=" + std::to_string(base_gamma) +
                             " (bin_scaled=" + std::string(gamma_bin_scaled ? "true" : "false") +
                             "), initial gamma=" + std::to_string(gamma));

            die_size = min( grid.getDieWidth(), grid.getDieHeight() );

            // PL param_scheduler drop-in (S6 step 0): seed the module's config + state from the same
            // knobs the native schedule uses, now that base_gamma is finalized. dff is passed in each
            // call (from density_force_fraction), so dff_coef is unused here.
            use_pl_scheduler = cfg["params"].value("use_pl_scheduler", false);
            pl_sched_params.base_gamma       = base_gamma;
            pl_sched_params.min_step         = cfg["params"]["density_weight_min_step"];
            pl_sched_params.max_step         = cfg["params"]["density_weight_max_step"];
            pl_sched_params.init_multiplier  = cfg["params"]["density_weight_init_multiplier"];
            pl_sched_params.dff_coef         = 0.0f; // unused: dff passed in from density_force_fraction
            pl_sched_params.enable_momentum  = enable_momentum ? 1 : 0;
            pl_sched_params.gamma_schedule   = gamma_schedule ? 1 : 0;
            pl_sched_params.overflow_threshold = overflow_threshold;
            pl_sched_params.min_iters        = min_iterations;
            pl_sched_params.max_iters        = max_iterations;
            pl_sched_params.conv_iters       = convergence_iterations;
            pl_sched_params.max_life         = 30; // MAX_LIFE
            plalgo::sched_state_init(pl_sched_state, pl_sched_params);

            #ifdef CREATE_VISUALIZATION
                initializeFocus();
                if(cfg["output"]["visualize"])
                    viz.init(db.getDieArea());
            #endif
        }

        
/**
 * @brief Estimate ᾱ_k from current positions vs stored prev-iteration data.
 *
 * Reads: node positions, prev_lookahead_pos (v_{k-1}), HPWL partials ∇f_pre(v_k),
 *        prev_lookahead_grad (∇f_pre(v_{k-1})).
 * Does NOT modify any state — call updateBBState() after the committed nudge.
 *
 * @return Raw Barzilai-Borwein step estimate ‖Δv‖/‖Δg‖ (no clamp — matches XPlace).
 */
float Placer::computeLipshitzEstimate()
{
    float pos_norm_sq  = 0.0f;
    float grad_norm_sq = 0.0f;

    auto accumulate = [&](Node* node) {
        // ||v̂_{k+1} - v_k||²
        float dx = node->next.probe_pos.x - node->current.probe_pos.x;
        float dy = node->next.probe_pos.y - node->current.probe_pos.y;
        pos_norm_sq += dx*dx + dy*dy;

        // ||∇f(v̂_{k+1}) - ∇f(v_k)||² of the PRECONDITIONED gradient — must match Node::step,
        // which moves by (1/precond_weight)·grad. The BB estimate α=‖Δv‖/‖Δg‖ is only valid when
        // Δg is the same map that is stepped; using the raw gradient here makes α too small when
        // preconditioning is on (P≥1 ⇒ raw grad larger), starving the step so density never spreads.
        // precond_weight is fixed within the iteration, so dividing the difference is exact; it is
        // 1.0 when preconditioning is off, leaving that path unchanged.
        float inv_pw = 1.0f / node->precond_weight;
        float dgx = inv_pw * (node->next.probe_grad.x - node->current.probe_grad.x);
        float dgy = inv_pw * (node->next.probe_grad.y - node->current.probe_grad.y);
        grad_norm_sq += dgx*dgx + dgy*dgy;
    };

    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        accumulate(item.second);
    }
    for (auto filler : db.getFillers())
        accumulate(filler);

    last_pos_norm_sq  = pos_norm_sq;   // exposed for the schedule-trace dump (PL param_scheduler port)
    last_grad_norm_sq = grad_norm_sq;
    float estimate = sqrtf(pos_norm_sq) / sqrtf(grad_norm_sq + 1e-8f);
    Logger::log_detail("New steplength estimate: " + PREC_P(estimate, 4));
    // No magnitude clamp — mirrors XPlace (nesterov_optimizer.py), which uses the raw
    // Barzilai-Borwein ratio ‖Δv‖/‖Δg‖ and relies solely on the backtracking line search to
    // reject over-aggressive steps. The estimate already self-scales with preconditioning because
    // Δg is the preconditioned gradient difference, so no precond-dependent cap is needed.
    return estimate;
}


/**
 * @brief Update density_weight to increase density force over time.
 * Numerically analyze the current health of the optimization:
 * Is HPWL improving?
 * If yes, continue increasing density_weight slowly.
 * If not, slow or decrease density_weight to allow HPWL forces to have more influence and escape local minima.
 */
void Placer::updateDensityWeight()
{
    if (hpwl_history.size() < 2) return; // need a previous HPWL to measure the trend

    // XPlace step_density_weight (param_scheduler.py). The every-3rd-iteration slow-phase throttle
    // is now applied by the shared skip_update flag in performIteration (matching Xplace step(),
    // which freezes λ, gamma, and precond_coef together), so this function updates unconditionally
    // once called.
    float current_hpwl = hpwl_history.back();
    float prev_hpwl    = hpwl_history[hpwl_history.size() - 2];
    float delta_hpwl   = current_hpwl - prev_hpwl;

    float dw_min_step = cfg["params"]["density_weight_min_step"]; // μ lower clamp (0.95)
    float dw_max_step = cfg["params"]["density_weight_max_step"]; // μ growth base (1.05)

    // μ > 1 grows λ. Grow near the max rate (decaying toward 0.98·max) while wirelength is
    // still improving; damp toward ~1.0 once wirelength worsens, so density does not
    // overshoot and blow HPWL up (the late-run divergence observed before this change).
    //
    // Worsening branch damping — must be SCALE-INVARIANT. Default = relative form
    // (mu ~ 1.05^(-(delta_hpwl/prev_hpwl)*100)): delta_hpwl/prev_hpwl is dimensionless, so the
    // damping behaves the same regardless of a design's absolute HPWL magnitude.
    //
    // History: a fixed constant K=350000 was tried to match XPlace step_density_weight
    // (param_scheduler.py:280) exactly, assuming sw_only's raw-DBU HPWL is XPlace's
    // round(hpwl*die_scale/site_width) frame (~1e7-1e8). That assumption is FALSE for standard-cell
    // designs (raw HPWL 5e8-1e9): delta_hpwl/350000 becomes O(1), mu pins to the 0.95 floor, and
    // lambda can never ramp -> the placement collapses and never spreads (des_perf/edit_dist/
    // superblue stalls at overflow ~0.8). So the fixed-K default is reverted. Config
    // `density_weight_worsening_hpwl_norm` still exposes K: set it >0 to use the fixed-K form (only
    // correct if delta_hpwl is first site-width-normalized into XPlace's frame).
    float worsening_hpwl_norm = cfg["params"].value("density_weight_worsening_hpwl_norm", -1.0f);
    float mu;
    if (delta_hpwl < 0.0f) {
        mu = dw_max_step * std::max(std::pow(0.9999f, (float)iteration), 0.98f);
    } else if (worsening_hpwl_norm > 0.0f) {
        mu = dw_max_step * std::clamp(std::pow(dw_max_step, -delta_hpwl / worsening_hpwl_norm),
                                      dw_min_step, dw_max_step);
    } else {
        float rel_worsening = delta_hpwl / (prev_hpwl + 1e-8f);
        mu = dw_max_step * std::clamp(std::pow(dw_max_step, -rel_worsening * 100.0f),
                                      dw_min_step, dw_max_step);
    }
    density_weight *= mu;

    // Emergency 2x jolt: if overflow has plateaued at a high value, double density_weight
    // to break out of the stall. Modeled after XPlace's enlarge_density mechanism.
    // (param_scheduler.py lines 293-304)
    int plateau_window = cfg["params"]["adaptation_window"];
    float plateau_threshold = cfg["params"]["slow_improvement_threshold"];
    float high_ovfw = cfg["params"]["high_overflow_threshold"];
    // Xplace uses 1000 (effectively once per run). Lower it to let the jolt re-fire and
    // repeatedly double lambda through a stall (a deliberate deviation from Xplace).
    int min_jolt_interval = cfg["params"].value("density_jolt_interval", 1000);

    if (iteration > plateau_window &&
        iteration - last_density_jolt_iter >= min_jolt_interval &&
        ovfw_history.back() > high_ovfw &&
        checkOverflowPlateau(plateau_window, plateau_threshold))
    {
        density_weight *= 2.0f;
        last_density_jolt_iter = iteration;
        Logger::log_info("Overflow plateau detected (ovfw=" +
            PREC(ovfw_history.back()) + "), 2x density weight jolt -> " +
            PREC(density_weight));
    }

    // Escalate preconditioner: double precond_coef every 20 iterations once overflow < 0.3
    // This progressively tightens macro movement in late placement (XPlace param_scheduler.py:340-347)
    if (enable_preconditioning && precond_coef_escalation &&
        ovfw_history.back() < 0.3f && precond_coef < 1024.0f) {
        if (iteration % 20 == 0) {
            precond_coef *= 2.0f;
            Logger::log_detail("Preconditioner escalation: precond_coef=" + PREC(precond_coef));
        }
    }
}


/**
 * @brief Check if overflow has plateaued over a recent window.
 *
 * Returns true if the relative range (max - min) / mean of the last
 * `window` overflow values is below `threshold`. Matches XPlace's
 * check_plateau() in param_scheduler.py.
 */
bool Placer::checkOverflowPlateau(int window, float threshold)
{
    if ((int)ovfw_history.size() < window) return false;
    auto begin = ovfw_history.end() - window;
    auto end = ovfw_history.end();
    float min_val = *std::min_element(begin, end);
    float max_val = *std::max_element(begin, end);
    float mean_val = std::accumulate(begin, end, 0.0f) / window;
    return (max_val - min_val) / (mean_val + 1e-8f) < threshold;
}


/**
 * @brief Detect divergence, mirroring XPlace check_divergence (param_scheduler.py).
 *
 * Fires only once the recent HPWL mean has climbed meaningfully above the best known
 * HPWL (so the healthy early phase, where HPWL rises as cells spread, is not flagged)
 * AND overflow is no longer making progress — it has grown past its best, plateaued
 * high, or is fluctuating upward. Reference is the primary best (converged) if we have
 * one, else the lowest-overflow fallback so the guard is useful even before convergence.
 */
bool Placer::checkDivergence(int window, float threshold)
{
    // Reference the CONVERGED best only (XPlace check_divergence returns False while
    // best_metric["hpwl"] is inf). best_fallback tracks the newest lowest-overflow point,
    // so on a healthy monotonic descent a trailing-mean-vs-newest comparison always reads
    // "worse than best" on both HPWL and overflow and false-fires the guard — that killed
    // adaptec2 at iter 332 with overflow still dropping ~2%/iter toward the 0.07 threshold.
    if (!best_primary.valid) return false;
    const BestSolution& best = best_primary;
    if ((int)hpwl_history.size() <= window) return false;

    auto hpwl_begin = hpwl_history.end() - window;
    float wl_mean = std::accumulate(hpwl_begin, hpwl_history.end(), 0.0f) / window;
    float wl_ratio = (wl_mean - best.hpwl) / (best.hpwl + 1e-8f);
    if (wl_ratio <= threshold * 1.2f)
        return false;  // HPWL still near its best → not diverging

    // HPWL is rising above best; classify by how overflow behaves over the window.
    auto ovfw_begin = ovfw_history.end() - window;
    float ovfw_mean = std::accumulate(ovfw_begin, ovfw_history.end(), 0.0f) / window;
    float ovfw_min  = *std::min_element(ovfw_begin, ovfw_history.end());
    float ovfw_max  = *std::max_element(ovfw_begin, ovfw_history.end());
    int rises = 0;
    for (auto it = ovfw_begin + 1; it != ovfw_history.end(); ++it)
        if (*it > *(it - 1)) rises++;
    float ovfw_up_frac = (float)rises / (window - 1);

    float ovfw_ratio = (ovfw_mean - std::max(overflow_threshold, best.overflow)) /
                       (best.overflow + 1e-8f);

    if (ovfw_ratio > threshold)                                  return true; // overflow grew past best
    if ((ovfw_max - ovfw_min) / (ovfw_mean + 1e-8f) < threshold) return true; // plateaued high
    if (ovfw_up_frac > 0.6f)                                     return true; // fluctuating upward
    return false;
}


/**
 * @brief Update per-node preconditioner weights (diagonal preconditioner).
 *
 * Each node's gradient is divided by its precond_weight before the Nesterov step.
 * weight = max(1.0, num_pins + precond_coef * density_weight * area)
 *
 * Large macros (many pins, large area) get heavy damping, while standard cells
 * are barely affected. The precond_coef escalates over time (see updateDensityWeight),
 * progressively tightening macro movement as placement matures.
 *
 * Reference: XPlace param_scheduler.py:349-364, calculator.py:5-8
 */
void Placer::updatePrecondWeights()
{
    float lambda_area_coef = precond_coef * density_weight * precond_density_scale;

    // Accumulate the two force-mass components for density_force_fraction:
    //   a1 = wirelength mass (pin count per node), a2 = density mass (λ · normalized area).
    // density_force_fraction = ‖a2‖₁ / (‖a1‖₁ + ‖a2‖₁) ∈ [0,1] measures how balanced the
    // wirelength and density forces are — the scale-invariant progress signal that drives
    // the density-weight schedule (XPlace param_scheduler.update_precond_weight, "weighted_weight").
    // Computed even when preconditioning is disabled, since the schedule still consumes it.
    float a1_norm = 0.0f, a2_norm = 0.0f;

    // Area term for a2 (precond_weight + area-mass dff). precond_raw_area=false: legacy area/avg_node_size
    // (keeps a2 O(1) per cell). true: RAW area, matching XPlace alpha_2 = pcoef·λ·mov_node_area — the
    // coordinate-scale-invariant form (sw_only runs in the same raw-DBU frame as XPlace).
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        Node* node = item.second;
        float num_pins = (float)node->getNets().size();
        float area = precond_raw_area ? node->getArea() : node->getArea() / avg_node_size;
        float a2 = lambda_area_coef * area;
        a1_norm += num_pins;
        a2_norm += a2;
        if (enable_preconditioning)
            node->precond_weight = std::max(1.0f, num_pins + a2);
    }
    for (auto filler : db.getFillers()) {
        float area = precond_raw_area ? filler->getArea() : filler->getArea() / avg_node_size;
        float a2 = lambda_area_coef * area;
        a2_norm += a2;  // fillers carry no pins, so they add no wirelength mass
        if (enable_preconditioning)
            filler->precond_weight = std::max(1.0f, a2);
    }

    // density_force_fraction: force-magnitude ratio (dff_force_ratio, field-norm invariant) or the
    // legacy area-mass ratio. The force ratio uses the PREVIOUS iteration's committed gradient L1 norms
    // (last_g*_L1, refreshed each combineGradients / seeded by initializeDensityWeight on iteration 1),
    // since updatePrecondWeights runs before this iteration's performNextStep→combineGradients.
    if (dff_force_ratio)
        density_force_fraction = last_gden_L1 / (last_gwl_L1 + last_gden_L1 + 1e-8f);
    else
        density_force_fraction = a2_norm / (a1_norm + a2_norm + 1e-8f);

    precond_a1_norm = a1_norm; // instrumentation: expose the two preconditioner addend norms for the trace
    precond_a2_norm = a2_norm;
}


/**
 * @brief Reset all nodes and nets in preparation for the next iteration
 */
void Placer::iterationReset()
{
    grid.iterationReset();
    db.iterationReset();
}


/**
 * @brief Initialize placement of all moveable nodes randomly, clustered about the target position
 *
 * @param target_pos Position around which nodes are spread
 * @param min_dist Minimum distance from target_pos a node can appear
 * @param max_dist Maximum distance from target_pos a node can appear
 */
void Placer::initializePlacement(Position target_pos, int min_dist, int max_dist)
{
    Logger::log_trace("Begin initializePlacement()");
    // Fixed seed (random_seed >= 0) gives identical initial placement across runs — needed for
    // controlled A/B tests; default -1 keeps the time-based seed.
    int seed = cfg["params"].value("random_seed", -1);
    std::srand(seed >= 0 ? (unsigned)seed : (unsigned)std::time(nullptr));

    Table top;
    top.add_row(RowStream{} << "Initial Placement");
    Table data;
    data.add_row(RowStream{} << "Center" << target_pos.x << target_pos.y);
    data.add_row(RowStream{} << "Min dist" << min_dist);
    data.add_row(RowStream{} << "Max dist" << max_dist); 
    top.add_row({data});
    top.format().font_align(FontAlign::center);
    Logger::log_info(top);

    float bin_area_16th = grid.getBinWidth() * grid.getBinHeight() / 16;
    int placed_count = 0, randomized_count = 0;

    // init_method selects how UNPLACED movable cells are seeded:
    //   "uniform_box"   — legacy: uniform offset within a box of half-width max_dist.
    //   "random_center" — XPlace-style tight Gaussian cluster at the die center
    //                    (per-axis sigma = 0.001 * die span). Relies on the density
    //                    force to spread the near-coincident cells over early iterations.
    std::string init_method = cfg["params"].value("init_method", std::string("uniform_box"));
    Logger::log_info("Movable-cell init_method: " + init_method);
    std::mt19937 gauss_gen(seed >= 0 ? (unsigned)seed : (unsigned)std::time(nullptr));
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    float gauss_sigma_x = grid.getDieWidth() * 0.001f;
    float gauss_sigma_y = grid.getDieHeight() * 0.001f;

    for (auto item : db.getComponents()) {
        Component* comp = item.second;

        // If the benchmark provides an initial placement (PLACED status from DEF),
        // use it — this gives a much better starting point than random center placement.
        // Only randomize truly UNPLACED components.
        if (comp->getStatus() == PLACED || comp->getStatus() == FIXED) {
            comp->initializeState(comp->next.node_pos); // use position from DEF parser
            placed_count++;
        } else {
            Position init_pos;
            if (init_method == "random_center") {
                init_pos = target_pos + Position(gauss(gauss_gen) * gauss_sigma_x,
                                                 gauss(gauss_gen) * gauss_sigma_y);
            } else {
                int range = std::max(1, max_dist - min_dist);
                int x_offset = min_dist + rand() % range;
                if(rand()%2 == 1) x_offset *= -1;
                int y_offset = min_dist + rand() % range;
                if(rand()%2 == 1) y_offset *= -1;
                init_pos = target_pos + Position(x_offset, y_offset);
            }
            comp->initializeState(init_pos);
            randomized_count++;
        }

        comp->checkIfLarge(bin_area_16th);
    }
    Logger::log_info("Initial placement: " + std::to_string(placed_count) +
                     " from benchmark, " + std::to_string(randomized_count) + " randomized");

    // Place Fillers uniformly at random across the whole die (XPlace get_filler_pos):
    // fillers must represent whitespace everywhere, so unlike the real cells they are NOT
    // clustered at the center — spreading them seeds the density model with the vacant
    // regions that the real cells should eventually flow into.
    for (auto filler : db.getFillers()) {
        Position init_pos(rand() % grid.getDieWidth(), rand() % grid.getDieHeight());
        filler->initializeState(init_pos);
    }

    // Initialize IO pad state (fixed, but need valid probe_pos for gradient computation)
    for (auto item : db.getIOPads())
        item.second->initializeState(item.second->next.node_pos);


    //printIterationResults(); // Prints "iteration 0" starting statistics
    iteration = 0;

    // These macros won't be on any nets, but they will add to the density computation
    // and could be created en masse at hotspot areas to gently push other nodes away.
}

/**
 * @brief Set initial density_weight based on ratio of total HPWL gradient to density gradient
 * Compute total force produced by the HPWL gradient,
 * and the total force produced by the density gradient.
 * Set initial density_weight as the ratio between these two to make them roughly balanced,
 * multiplied by a small number which makes the density force initially weaker
 */
void Placer::initializeDensityWeight()
{
    float HPWL_L1_norm = 0.0f;
    float density_L1_norm = 0.0f;
    for(auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        Node* node_p = item.second;

        Gradient& partials = node_p->next.probe_grad;  // HPWL-only (before combineGradients)
        HPWL_L1_norm += fabs(partials.x) + fabs(partials.y);

        Gradient electro_force = computeElectrostaticForce(node_p);
        density_L1_norm += fabs(electro_force.x) + fabs(electro_force.y);
    }
    // Fillers carry density force but no wirelength; XPlace's init balance counts them.
    for (auto filler : db.getFillers()) {
        Gradient electro_force = computeElectrostaticForce(filler);
        density_L1_norm += fabs(electro_force.x) + fabs(electro_force.y);
    }

    float initial_multiplier = cfg["params"]["density_weight_init_multiplier"];
    density_weight = (HPWL_L1_norm / (density_L1_norm + 1e-8f)) * initial_multiplier;
    last_gwl_L1  = HPWL_L1_norm;    // exposed for the PL param_scheduler iteration-1 lambda init
    last_gden_L1 = density_L1_norm;

    Logger::log_info("Initial HPWL gradient L1 norm: " + std::to_string(HPWL_L1_norm));
    Logger::log_info("Initial density gradient L1 norm: " + std::to_string(density_L1_norm));
    Logger::log_info("Initialized density_weight: " + std::to_string(density_weight));
}



/**
 * @brief Check if the placement algorithm has converged
 *
 * Convergence is determined by two criteria that must both be met:
 * 1. Overflow is below the threshold (relative to total cell area)
 * 2. HPWL improvement over a window of iterations is below the threshold
 *
 * Also enforces minimum iteration count and uses maximum iterations as a fallback.
 *
 * @return true if converged (or max iterations reached), false otherwise
 */
bool Placer::checkConvergence()
{
    // Safety fallback: max iterations
    if (iteration >= max_iterations) {
        Logger::log_info("Stopping: reached maximum iterations (" +
                        std::to_string(max_iterations) + ")");
        return true;
    }

    // Enforce minimum iterations before any checks
    if (iteration < min_iterations)
        return false;

    float overflow = ovfw_history.back();
    float current_hpwl = hpwl_history.back();

    // NaN detection — stop immediately
    if (std::isnan(overflow) || std::isnan(current_hpwl)) {
        Logger::log_info("Stopping: NaN detected at iteration " +
                        std::to_string(iteration));
        return true;
    }

    // Divergence guard (XPlace need_to_early_stop / check_divergence).
    const BestSolution& best_ref = best_primary.valid ? best_primary
                                 : best_fallback.valid ? best_fallback
                                 : best_primary;

    // Coarse backstop: HPWL has blown past 2x the best known solution.
    if (best_ref.valid && current_hpwl > 2.0f * best_ref.hpwl) {
        Logger::log_info("Stopping: divergence detected at iteration " +
                        std::to_string(iteration) +
                        " (HPWL " + std::to_string(current_hpwl) +
                        " > 2x best " + std::to_string(best_ref.hpwl) +
                        " from iter " + std::to_string(best_ref.iteration) + ")");
        return true;
    }

    // Fine-grained guard, armed only once the run is in the near-converged band
    // (overflow < 5x the stop threshold). During the high-overflow spreading phase
    // HPWL naturally rises and overflow is noisy, which would otherwise be misread as
    // divergence. Each detection burns 6 life; a hard overflow plateau ends it outright.
    if (iteration > 100 && best_ref.valid && overflow < 5.0f * overflow_threshold) {
        if (checkDivergence(3, 0.01f * overflow))
            life -= 6;
        if (overflow >= overflow_threshold && checkOverflowPlateau(50, 0.05f))
            life -= MAX_LIFE;
        if (life <= 0) {
            Logger::log_info("Stopping: divergence guard exhausted at iteration " +
                            std::to_string(iteration) + " (best HPWL " +
                            std::to_string(best_ref.hpwl) + " from iter " +
                            std::to_string(best_ref.iteration) + ")");
            return true;
        }
    }

    // Stagnation detection: DISABLED for now — need to let density weight grow
    // longer before declaring stagnation. Re-enable once density schedule is tuned.
    // static constexpr int STAGNATION_WINDOW = 50;
    // if ((int)hpwl_history.size() > STAGNATION_WINDOW + 1) {
    //     float old_hpwl = hpwl_history[hpwl_history.size() - STAGNATION_WINDOW - 1];
    //     float old_ovfw = ovfw_history[ovfw_history.size() - STAGNATION_WINDOW - 1];
    //     bool hpwl_worsening = current_hpwl > old_hpwl * 1.01f;
    //     bool ovfw_not_improving = overflow > old_ovfw * 0.95f;
    //     if (hpwl_worsening && ovfw_not_improving) {
    //         Logger::log_info("Stopping: stagnation detected at iteration " +
    //                         std::to_string(iteration) +
    //                         " (HPWL worsening: " + std::to_string(old_hpwl) +
    //                         " -> " + std::to_string(current_hpwl) +
    //                         ", overflow stalled: " + std::to_string(old_ovfw) +
    //                         " -> " + std::to_string(overflow) +
    //                         " over last " + std::to_string(STAGNATION_WINDOW) + " iters)");
    //         return true;
    //     }
    // }

    // Overflow countdown — XPlace-inspired convergence mechanism
    // Once overflow drops below threshold, count down convergence_iterations then stop.
    if (overflow < overflow_threshold) {
        if (convergence_iterations_remaining < 0) {
            // First crossing below threshold — start countdown
            convergence_iterations_remaining = convergence_iterations;
            Logger::log_info("Overflow below " + PREC(overflow_threshold) +
                            ", starting convergence countdown (" +
                            std::to_string(convergence_iterations) + " iterations)");
        }
        convergence_iterations_remaining--;

        // Run the full countdown after smoothed overflow first crosses the threshold, rather
        // than stopping on the first crossing. HPWL is already plateaued by this point, so a
        // HPWL-plateau early-out would stop immediately and leave overflow (and thus the
        // physical spread) worse than it needs to be; the countdown lets overflow keep
        // dropping toward XPlace's converged regime. (Mirrors XPlace's post-threshold life.)
        if (convergence_iterations_remaining <= 0) {
            Logger::log_info("Convergence achieved at iteration " +
                            std::to_string(iteration) +
                            " (overflow countdown complete)");
            return true;
        }

        Logger::log_detail("Convergence countdown: " +
                          std::to_string(convergence_iterations_remaining) + " remaining");
    } else {
        // Overflow rose back above threshold — reset countdown
        if (convergence_iterations_remaining >= 0) {
            Logger::log_detail("Overflow rose above threshold, resetting convergence countdown");
            convergence_iterations_remaining = -1;
        }
    }

    return false;
}


/**
 * @brief Subtract electrostatic force from probe_grad in-place to form total gradient.
 *
 * Called after computeAllPartials() which accumulates HPWL into next.probe_grad.
 * For components: next.probe_grad -= electro (electro already includes λ weighting)
 * For fillers: next.probe_grad = -electro (no HPWL partials)
 */
void Placer::combineGradients()
{
    // Refresh the committed-gradient L1 norms while both force components are in hand:
    // last_gwl_L1 = Σ‖∇wl‖₁ (probe_grad before the subtraction), last_gden_L1 = Σ‖λ·∇den‖₁ (the
    // electrostatic force, which already carries λ). The force-ratio dff (next iteration) reads these.
    float gwl_L1 = 0.0f, gden_L1 = 0.0f;
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        Gradient& g = item.second->next.probe_grad;
        gwl_L1 += fabsf(g.x) + fabsf(g.y);
        Gradient electro = computeElectrostaticForce(item.second);
        gden_L1 += fabsf(electro.x) + fabsf(electro.y);
        g -= electro;
    }
    for (auto filler : db.getFillers()) {
        Gradient electro = computeElectrostaticForce(filler);
        gden_L1 += fabsf(electro.x) + fabsf(electro.y);  // fillers carry density force, no wirelength
        filler->next.probe_grad -= electro;
    }
    last_gwl_L1  = gwl_L1;
    last_gden_L1 = gden_L1;
}


/**
 * @brief Clamp node positions so the entire cell stays within the die area.
 *
 * The node position is the lower-left corner of the cell, so the upper bound
 * must account for the cell's width/height to prevent the right/top edge from
 * extending past the die boundary.
 */
void Placer::enforceDieBoundaries(Node* node_p)
{
    float max_x = (float)grid.getDieWidth()  - node_p->getXsize();
    float max_y = (float)grid.getDieHeight() - node_p->getYsize();

    // Clamp node_pos
    node_p->next.node_pos.x = std::clamp(node_p->next.node_pos.x, 0.0f, max_x);
    node_p->next.node_pos.y = std::clamp(node_p->next.node_pos.y, 0.0f, max_y);

    // Clamp probe_pos
    node_p->next.probe_pos.x = std::clamp(node_p->next.probe_pos.x, 0.0f, max_x);
    node_p->next.probe_pos.y = std::clamp(node_p->next.probe_pos.y, 0.0f, max_y);
}


/**
 * @brief Save current positions and gradients as "previous" for BB estimate and backtracking.
 * Must be called before stepAllNodes() so prev fields serve as the restore point.
 */
void Placer::advanceIterationState()
{
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->cacheState();
    }
    for (auto filler : db.getFillers())  filler->cacheState();
}

/**
 * @brief Perform Nesterov gradient step for all nodes (Algorithm 1, Lines 2 & 4).
 *
 * Delegates to Node::step() which reads from current state and writes to next.
 * The momentum coefficient is computed by the caller (computeNextStep owns Nesterov state).
 *
 * @param mom_coeff Momentum coefficient: (a_k - 1) / a_{k+1}, or 0 if momentum disabled.
 */
void Placer::stepAllNodes()
{
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->step(step_length, momentum_coeff);
        enforceDieBoundaries(item.second);
    }
    for (auto filler : db.getFillers()) {
        filler->step(step_length, momentum_coeff);
        enforceDieBoundaries(filler);
    }
}

/**
 * @brief Algorithm 2 (BkTrk): Backtracking line search for step length.
 *
 * Uses the Barzilai-Borwein (Lipschitz) estimate to validate the step length.
 * The do-while loop takes trial steps with the current step_length, recomputes
 * gradients at the trial position, and checks if α̂ ≤ ε · fresh_bb.
 * If rejected, positions are restored and step_length is updated to the fresh estimate.
 *
 * After the loop, the accepted trial step is committed (positions already at u_{k+1}, v_{k+1})
 * and the Nesterov coefficient is advanced.
 */
void Placer::performNextStep(bool backtracking_enabled)
{
    // Algorithm 1, Line 3: compute momentum coefficient for this iteration
    float a_next = (1.0f + sqrtf(4.0f * nesterov_ak * nesterov_ak + 1.0f)) / 2.0f;
    momentum_coeff = enable_momentum ? (nesterov_ak - 1.0f) / a_next : 0.0f;
    nesterov_ak = a_next;

    // step_length (α̂) carries over from previous iteration (or warmup default)
    advanceIterationState();  // copy next state into current state

    // Algorithm 2: Backtracking
    int tries = 0;
    float prev_step_length;
    do {
        // Lines 2 & 3: trial step using existing step_length from previous iteration
        stepAllNodes();

        iterationReset();
        // Recompute gradients at trial v̂ (the expensive part, accelerated on AIEs)
        computeHpwlPartials();      // ∇HPWL at probe positions → next.probe_grad (HPWL-only)
        computeElectricFields();    // ∇D from ρ → bin eFields
        combineGradients();         // add electro in-place: next.probe_grad becomes total ∇f

        prev_step_length = step_length;

        // new steplength estimate at probe position: 
        // α = 1 / L = ||v̂ - v_k|| / ||∇f(v̂) - ∇f(v_k)||
        step_length = computeLipshitzEstimate();

        // Accept if α̂ ≤ ε · fresh_bb (step is not too aggressive)
    } while(backtracking_enabled &&
            prev_step_length > backtrack_epsilon * step_length && // epsilon condition
            ++tries < max_backtracking_attempts);

    backtrack_steps = tries; // for logging

    if (Logger::isKeyActive("DEBUG")) logStepDiagnostics();
}

void Placer::logStepDiagnostics()
{
    Logger::log_info("=== Step Diagnostics (iteration " + std::to_string(iteration) + ") ===");
    Logger::log_info("  step_length (α̂):  " + PREC_P(step_length, 6));
    Logger::log_info("  momentum_coeff:    " + PREC_P(momentum_coeff, 6));
    Logger::log_info("  nesterov_ak:       " + PREC_P(nesterov_ak, 6));
    Logger::log_info("  backtrack_steps:   " + std::to_string(backtrack_steps));

    // Gradient statistics (from current state used for stepping)
    float grad_L1 = 0.0f, max_grad = 0.0f;
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        Node* n = item.second;
        grad_L1 += fabs(n->current.probe_grad.x) + fabs(n->current.probe_grad.y);
        max_grad = std::max(max_grad, std::max(fabs(n->current.probe_grad.x), fabs(n->current.probe_grad.y)));
    }
    Logger::log_info("  grad L1 norm:      " + SCI(grad_L1));
    Logger::log_info("  max |grad|:        " + SCI(max_grad));
    Logger::log_info("  α̂ * max|grad|:     " + SCI(step_length * max_grad) + "  (max single-step displacement)");

    // Position delta statistics
    float max_node_delta = 0.0f, max_probe_overshoot = 0.0f;
    int clamped_count = 0;
    float die_w = (float)grid.getDieWidth(), die_h = (float)grid.getDieHeight();
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        Node* n = item.second;
        float dx = fabs(n->next.node_pos.x - n->current.node_pos.x);
        float dy = fabs(n->next.node_pos.y - n->current.node_pos.y);
        max_node_delta = std::max(max_node_delta, std::max(dx, dy));

        float ox = fabs(n->next.probe_pos.x - n->next.node_pos.x);
        float oy = fabs(n->next.probe_pos.y - n->next.node_pos.y);
        max_probe_overshoot = std::max(max_probe_overshoot, std::max(ox, oy));

        if (n->next.node_pos.x <= 0 || n->next.node_pos.x >= die_w ||
            n->next.node_pos.y <= 0 || n->next.node_pos.y >= die_h)
            clamped_count++;
    }
    Logger::log_info("  max |Δnode_pos|:   " + SCI(max_node_delta));
    Logger::log_info("  max probe overshoot: " + SCI(max_probe_overshoot));
    Logger::log_info("  nodes at boundary: " + std::to_string(clamped_count));

    // Sample trace: first 3 components
    int count = 0;
    for (auto item : db.getComponents()) {
        if (count >= 3) break;
        if (item.second->getStatus() == FIXED) continue;
        Node* n = item.second;
        Logger::log_info("  --- Sample node: " + n->getName() + " ---");
        Logger::log_info("    current.node_pos (u_k):   (" + PREC_P(n->current.node_pos.x, 2) + ", " + PREC_P(n->current.node_pos.y, 2) + ")");
        Logger::log_info("    current.probe_pos (v_k):  (" + PREC_P(n->current.probe_pos.x, 2) + ", " + PREC_P(n->current.probe_pos.y, 2) + ")");
        Logger::log_info("    current.probe_grad:       (" + SCI(n->current.probe_grad.x) + ", " + SCI(n->current.probe_grad.y) + ")");
        Logger::log_info("    α̂ * grad:                 (" + SCI(step_length * n->current.probe_grad.x) + ", " + SCI(step_length * n->current.probe_grad.y) + ")");
        Logger::log_info("    next.node_pos (u_{k+1}):  (" + PREC_P(n->next.node_pos.x, 2) + ", " + PREC_P(n->next.node_pos.y, 2) + ")");
        Logger::log_info("    next.probe_pos (v_{k+1}): (" + PREC_P(n->next.probe_pos.x, 2) + ", " + PREC_P(n->next.probe_pos.y, 2) + ")");
        Logger::log_info("    next.probe_grad:          (" + SCI(n->next.probe_grad.x) + ", " + SCI(n->next.probe_grad.y) + ")");
        count++;
    }
    Logger::log_info("=== End Step Diagnostics ===");
}

void Placer::snapshotBestPlacement()
{
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->best_solution_pos = item.second->next.node_pos;
    }
    for (auto filler : db.getFillers())
        filler->best_solution_pos = filler->next.node_pos;
}

void Placer::restoreBestPlacement()
{
    for (auto item : db.getComponents()) {
        if (item.second->getStatus() == FIXED) continue;
        item.second->next.node_pos = item.second->best_solution_pos;
    }
    for (auto filler : db.getFillers())
        filler->next.node_pos = filler->best_solution_pos;
}

AIEPLACE_NAMESPACE_END