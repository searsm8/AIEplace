/**
 * @file Output.cpp
 * @brief Output, reporting, and results management (DEF writeout, CSV/summary, visualization
 *        export). Split out of AIEplace.cpp.
 */

#include "AIEplace.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <random>
#include <set>

AIEPLACE_NAMESPACE_BEGIN

using namespace tabulate; // table types, scoped to this .cpp (not leaked via Logger.h)

void Placer::printWelcomeBanner(bool show_info)
{
    // Decoration: terminal only. A piped log opens on the first real message instead.
    if (!interactive || quiet)
        return;

    // Raw string logo
    string logo = R"(
╔══════════════════════════════════════════════════════╗
║    _____   ___               __                      ║
║   /  _  \ │   │ _____ _____ │  │_____   ____   ____  ║
║  /  /_\  \│   ││  __/|     \│  │\__  \ / ___\ / __ \ ║
║ /         \   ││  _/ |  ──  │  │_/ __ \\ \___/  ___/ ║
║ \____│____/___││____\|   __/│____\_____/\____/\____/ ║
╠══════════════════════|  /════════════════════════════╣
╚══════════════════════|_/═════════════════════════════╝ )";

    Table banner;
    banner.add_row({logo});
    banner.format()
        .width(59)
        .hide_border()
        .font_color(Color::white)
        .font_align(FontAlign::left);

    if(show_info)
    {
        Table info;
        info.add_row({"Version:", AIEPLACE_VERSION});
        info.format().hide_border();
        banner.add_row({info});
        banner.add_row({"VLSI global placement algorithm accelerated on AI Engines"});
        banner.add_row({}); // This line intentionally left blank
    }

    banner.print(cout);
}

void Placer::printIterationResults()
{
    printDSEInfoTable();

    float hpwl = hpwl_history.back();
    float overflow = ovfw_history.back();

    printIterationSummaryTable(hpwl, overflow);
    dumpIterationPositions();
    appendIterationLog(hpwl, overflow);
}

/// @brief Log the DSE sweep's parameter table (config output.DSE_info), if this run has one.
void Placer::printDSEInfoTable()
{
    if (!cfg["output"]["DSE_info"]) return;

    Table DSE_info;
    std::string dse_info_str = cfg["output"]["DSE_info"].value_or(std::string{});
    std::istringstream stream(dse_info_str);
    std::string line;

    while (std::getline(stream, line)) {
        auto eq = line.find('=');
        if (eq != std::string::npos)
            DSE_info.add_row(RowStream{} << line.substr(0, eq) << line.substr(eq + 1));
        else
            DSE_info.add_row(RowStream{} << line);
    }
    DSE_info.column(0).format().font_align(FontAlign::right);
    DSE_info.column(1).format().font_align(FontAlign::left);
    //DSE_info.format().hide_border();
    Logger::log_info(DSE_info);
}

/// @brief Log the per-iteration summary table (benchmark, HPWL, overflow, step length, etc.).
void Placer::printIterationSummaryTable(float hpwl, float overflow)
{
    // Live-status table: interactive-only, throttled to every X iterations. Always fires on
    // iteration 1 too, so a slow/large design gives immediate confirmation it's running instead
    // of going silent for the entire first X iterations (which reads as a hang).
    if (!interactive) return;
    int cadence = cfg["output"]["iterations_per_status"].value_or(10);
    if (iteration != 1 && iteration % cadence != 0) return;

    // Transposed: one header line of field names, one line of values, so the whole report
    // fits on 2 lines instead of one row per field. Header line only printed the first time.
    // Plain fixed-width text (not a tabulate Table) — nested tables don't reproduce identical
    // column widths across separate print calls, so this is the reliable way to keep the
    // header and every value line aligned, borderless, with no per-call width drift.
    static bool header_printed = false;
    static constexpr int col_widths[4] = {11, 11, 10, 13}; // last field (Density Weight) unpadded

    auto field = [](const std::string& text, int width) {
        std::ostringstream oss;
        oss << std::left << std::setw(width) << text;
        return oss.str();
    };

    if (!header_printed) {
        Logger::log_iter(field("Iteration", col_widths[0]) + field("HPWL", col_widths[1]) +
                          field("Overflow", col_widths[2]) + field("Step Length", col_widths[3]) +
                          "Density Weight");
        header_printed = true;
    }
    Logger::log_iter(field(std::to_string(iteration), col_widths[0]) + field(SCI(hpwl), col_widths[1]) +
                      field(PREC(overflow), col_widths[2]) + field(SCI(step_length), col_widths[3]) +
                      SCI(density_weight));
}

/// @brief Append this iteration's HPWL/overflow/step stats to iterations.dat, with a header on the first write.
void Placer::appendIterationLog(float hpwl, float overflow)
{
    //Logger::export_eField(grid, output_dir, iteration);

    std::ofstream hpwl_file;
    fs::path dir = output_dir;
    hpwl_file.open(dir.append("iterations.dat"), std::ios_base::app);

    // Add header if this is the first iteration
    if(iteration == 1)
        hpwl_file << "Iter, HPWL, OVFW, step_len, density_weight, BkSteps" << endl;

    hpwl_file << std::setfill('0') << std::setw(3) << iteration << ", "
              << SCI(hpwl) << ", "
              << SCI(overflow) << ", "
              << SCI(step_length) << ", "
              << SCI(density_weight) << ", "
              << backtrack_steps << endl;
    hpwl_file.close();
}

// Enhanced function to create organized output structure
void Placer::createRunOutputStructure()
{
    // Generate run ID and timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    // Create timestamp string (YYYYMMDD_HHMMSS_mmm)
    std::stringstream timestamp_ss;
    timestamp_ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
    timestamp_ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
    std::string timestamp = timestamp_ss.str();

    // Create directory structure: <results_dir>/<benchmark_name>/<timestamped_run_name>/
    output_dir = results_dir / db.getBenchmarkName() / (timestamp + "_" +
                                       ConfigUtils::require<std::string>(cfg, "params", "partials_compute_method") + "_" +
                                       ConfigUtils::require<std::string>(cfg, "params", "density_compute_method"));

    fs::create_directories(output_dir);

    // From here on the full-detail report captures everything; the backlog of lines logged
    // during setup (before this directory existed) is flushed into it now.
    Logger::openReport(output_dir);
    Logger::log_detail("Created output directory: " + output_dir.string());
}

void Placer::writeResultsCSV(float final_hpwl, float final_hpwl_exact, float final_overflow,
                              float total_runtime, float iteration_avg,
                              float hpwl_improvement, const std::string& run_id)
{
    if (!fs::exists(results_dir))
        fs::create_directories(results_dir);

    fs::path csv_path = results_dir / "results.csv";
    bool need_header = !fs::exists(csv_path);

    std::ofstream out_file;
    out_file.open(csv_path, std::ios_base::app);
    out_file.imbue(std::locale::classic());  // Prevent comma thousands separators

    float xplace_ref = lookupXplaceReferenceHPWL(db.getBenchmarkName());
    std::vector<std::pair<std::string, std::string>> dse_params = parseDSEParams();

    if (need_header)
        writeResultsCSVHeader(out_file, dse_params);

    writeResultsCSVRow(out_file, final_hpwl_exact, total_runtime, iteration_avg, dse_params, xplace_ref);

    out_file.close();
}

/**
 * @brief XPlace GP-ONLY reference HPWL (masked_hpwl at "GP Stop", NOT legalized), so the Ratio
 *        column is an honest GP-vs-GP comparison instead of our-GP vs XPlace-GP+legalization.
 *        Values from local XPlace runs: ~/phd/Xplace/result/<ts>_<design>/log/test.log line
 *        "GP Stop! ... masked_hpwl: X". Only ISPD2005 is populated: XPlace's ispd2005 HPWL
 *        shares sw_only's raw-DBU frame. ISPD2015 (mgc_*) XPlace HPWL is site-width-normalized
 *        (~ /site_width, e.g. /200) -- a DIFFERENT frame -- so it must NOT be mixed in here;
 *        populate mgc as masked_hpwl*site_width once measured. bigblue3/bigblue4 need a local
 *        XPlace GP run.
 * @return the reference HPWL, or 0.0f if this benchmark has no recorded reference.
 */
float Placer::lookupXplaceReferenceHPWL(const std::string& bench_name)
{
    static const std::map<std::string, float> xplace_hpwl = {
        {"adaptec1",  7.060218e+07f},
        {"adaptec2",  7.893496e+07f},
        {"adaptec3",  1.858436e+08f},
        {"adaptec4",  1.675808e+08f},
        {"bigblue1",  8.721903e+07f},
        {"bigblue2",  1.298895e+08f},
    };
    auto it = xplace_hpwl.find(bench_name);
    return (it != xplace_hpwl.end()) ? it->second : 0.0f;
}

/**
 * @brief Parse the DSE sweep's key=value lines (cfg["output"]["DSE_info"]) into pairs, skipping
 *        the first two lines (progress counter and benchmark name — redundant with other columns).
 */
std::vector<std::pair<std::string, std::string>> Placer::parseDSEParams()
{
    std::vector<std::pair<std::string, std::string>> dse_params;
    if (!cfg["output"]["DSE_info"])
        return dse_params;

    std::string dse_str = cfg["output"]["DSE_info"].value_or(std::string{});
    std::istringstream stream(dse_str);
    std::string line;
    int line_num = 0;
    while (std::getline(stream, line)) {
        if (++line_num <= 2) continue;
        auto eq = line.find('=');
        if (eq != std::string::npos)
            dse_params.push_back({line.substr(0, eq), line.substr(eq + 1)});
    }
    return dse_params;
}

/// @brief Write the results.csv column header, including one column per swept DSE parameter.
void Placer::writeResultsCSVHeader(std::ofstream& out_file,
                                    const std::vector<std::pair<std::string, std::string>>& dse_params)
{
    out_file << "Design,";
    out_file << "Iters,";
    out_file << "Best Iter,";
    out_file << "Best OVFW,";
    out_file << "Best GP HPWL,";
    out_file << "XPlace GP HPWL,";
    out_file << "Ratio,";
    // DSE sweep parameter columns (one per swept parameter)
    for (const auto& [key, val] : dse_params)
        out_file << key << ",";
    out_file << "Gamma,";
    out_file << "Net Count,";
    out_file << "Node Count,";
    out_file << "Final HPWL Exact,";   // final HPWL over ALL nets (no net-degree mask)
    // Phase 1 endpoint (TODO #13 two-phase runs only; N/A on a single-phase run) -- otherwise
    // a two-phase sweep shows only the phase-2 endpoint and the macro-placement quality phase 1
    // is responsible for is invisible.
    out_file << "Phase1 Iters,";
    out_file << "Phase1 HPWL,";
    out_file << "Phase1 OVFW Smoothed,";
    out_file << "Phase1 OVFW Exact,";
    out_file << "Phase1 Stop Reason,";
    out_file << "Total Runtime (sec),";
    out_file << "DB IO Time (sec),";
    out_file << "Algorithm Time (sec),";
    out_file << "Iteration Avg (sec),";
    out_file << "Memory Usage (MB),";
    out_file << "Output Dir,";
    out_file << "Timestamp";
    out_file << endl;
}

/// @brief Write one results.csv data row for this run.
void Placer::writeResultsCSVRow(std::ofstream& out_file, float final_hpwl_exact, float total_runtime,
                                 float iteration_avg,
                                 const std::vector<std::pair<std::string, std::string>>& dse_params,
                                 float xplace_ref)
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream timestamp;
    timestamp << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");

    out_file << "\"" << db.getBenchmarkName() << "\",";
    out_file << iteration << ",";
    // Best solution info (primary > fallback > N/A)
    const BestSolution& best = best_primary.valid ? best_primary
                             : best_fallback.valid ? best_fallback
                             : best_primary;
    if (best.valid) {
        out_file << best.iteration << ","
                 << std::scientific << PREC(best.overflow) << ","
                 << std::scientific << SCI(best.hpwl) << ",";
        // XPlace reference and ratio
        if (xplace_ref > 0.0f) {
            out_file << std::scientific << SCI(xplace_ref) << ","
                     << std::fixed << std::setprecision(2) << (best.hpwl / xplace_ref) << ",";
        } else {
            out_file << "N/A,N/A,";
        }
    } else {
        out_file << "N/A,N/A,N/A,N/A,N/A,";
    }
    // DSE sweep parameter values (matches header columns)
    for (const auto& [key, val] : dse_params)
        out_file << val << ",";
    out_file << PREC(gamma) << ",";
    out_file << db.getNetsVector().size() << ",";
    out_file << db.getComponents().size() << ",";
    out_file << std::scientific << SCI(final_hpwl_exact) << ",";   // Final HPWL Exact (all nets)
    if (m_phase1_summary.valid) {
        out_file << m_phase1_summary.iterations << ","
                 << std::scientific << SCI(m_phase1_summary.hpwl) << ","
                 << std::scientific << PREC(m_phase1_summary.overflow_smoothed) << ","
                 << std::scientific << PREC(m_phase1_summary.overflow_exact) << ","
                 << stopReasonName(m_phase1_summary.stop_reason) << ",";
    } else {
        out_file << "N/A,N/A,N/A,N/A,N/A,";
    }
    out_file << std::fixed << std::setprecision(3);
    out_file << total_runtime << ",";
    out_file << Logger::getFunctionTime("setupDesign") / 1.0e6 << ",";
    out_file << algo_time << ",";
    out_file << iteration_avg << ",";
    out_file << getMemoryUsageMB() << ",";
    out_file << "\"" << output_dir.string() << "\",";
    out_file << "\"" << timestamp.str() << "\"";
    out_file << endl;
}

/// @brief Stable token for the run's termination mode (see StopReason); grep target for sweeps.
const char* Placer::stopReasonName(StopReason reason)
{
    switch (reason) {
        case StopReason::CONVERGED:        return "converged";
        case StopReason::MAX_ITERATIONS:   return "max_iterations";
        case StopReason::NAN_METRICS:      return "nan_metrics";
        case StopReason::NAN_PARTIALS:     return "nan_partials";
        case StopReason::DIVERGED_HPWL:    return "diverged_hpwl";
        case StopReason::DIVERGENCE_GUARD: return "divergence_guard";
        case StopReason::RUNNING:          break;
    }
    return "unknown";
}

void Placer::printFinalResults()
{
    Logger::log_info("AIEplace algorithm complete.");
    // One canonical, machine-readable line per run. Every stop path already logs its own
    // prose; this is the one a sweep runner greps.
    Logger::log_info("[STOP] reason=" + std::string(stopReasonName(m_stop_reason))
                   + " iteration=" + std::to_string(iteration));

    BestSolution& chosen = restoreBestSolution();

    // Use the output directory created in constructor
    std::string run_output_dir = output_dir.string();
    std::string run_id = generateRunId();

    FinalMetrics metrics = computeFinalMetrics();
    logOverflowDiagnostics();
    dumpBestPlacementDensity();

    exportSummaryReports(chosen, metrics, run_output_dir);

    // Write run record to global results CSV
    writeResultsCSV(metrics.final_hpwl, metrics.final_hpwl_exact, metrics.final_overflow,
                    metrics.total_runtime, metrics.iteration_avg, metrics.hpwl_improvement, run_id);

    // The restored best placement, tagged so the offline tool can render the same picture the
    // cairo renderer writes as best_solution.png. That shared final frame is what step 2's
    // C++-vs-Python comparison is anchored on (handoff §6).
    dumpIterationPositions("best_solution");

    writeFinalDesignArtifacts(run_output_dir);
    finalizePositionDump();

    Logger::log_info("All outputs saved to: " + run_output_dir);
}

/**
 * @brief Select the best solution to report: primary (HPWL-driven, converged) > fallback
 *        (Pareto) > the last solution reached (BestSolution left invalid; restore is then a
 *        no-op). Restores the chosen placement's positions.
 */
Placer::BestSolution& Placer::restoreBestSolution()
{
    BestSolution& chosen = best_primary.valid ? best_primary
                         : best_fallback.valid ? best_fallback
                         : best_primary;

    if (chosen.valid) {
        restoreBestPlacement();
        std::string type = (&chosen == &best_primary) ? "primary (converged)" : "fallback (lowest overflow)";
        Logger::log_info("Restored " + type + " best placement from iteration " +
            std::to_string(chosen.iteration) +
            " (HPWL: " + std::to_string(chosen.hpwl) +
            ", overflow: " + std::to_string(chosen.overflow) + ")");
    } else {
        Logger::log_info("No best placement saved (solver may not have stabilized). Using last solution.");
    }
    return chosen;
}

/// @brief Compute the run's headline HPWL/overflow/timing metrics on the restored placement.
Placer::FinalMetrics Placer::computeFinalMetrics()
{
    FinalMetrics m;

    algo_time = Logger::getFunctionTime("run") / 1e6; // Convert microseconds to seconds
    m.final_hpwl = db.computeTotalWirelength(ConfigUtils::require<std::string>(cfg, "params", "wirelength_method"), cfg["params"]["ignore_net_degree"].value_or(100));
    // Exact HPWL over ALL nets (no net-degree mask). The masked final_hpwl above drops nets with
    // > ignore_net_degree pins (matching XPlace's GP metric); this includes them, matching XPlace's
    // post-GP "exact HPWL" / published-number convention (get_obj_hpwl is unmasked). Report both:
    // masked for the schedule + GP-vs-GP, exact for the apples-to-apples vs XPlace's headline HPWL.
    // A huge cap (not -1, which would exclude every net) includes every degree.
    m.final_hpwl_exact = db.computeTotalWirelength(ConfigUtils::require<std::string>(cfg, "params", "wirelength_method"), 1000000000);
    // Exact (physical) overflow — sharp footprints, the real spreading quality. Fillers are
    // INCLUDED: XPlace's reported overflow (evaluate_placement) counts filler density, so this
    // is the directly XPlace-comparable headline number. Excluding them reads ~2x low.
    m.final_overflow = computeOverflow(false, nullptr, true);
    // The smoothed overflow the run actually converged on — same filler policy as the
    // convergence signal (recordIterationResults), so the report explains why it stopped.
    m.final_smoothed_overflow = computeOverflow(true, nullptr, convergenceIncludesFillers());
    // Macro-excluded, sharp, no filler: the number comparable to XPlace's Mixed-GP reference
    // (see logOverflowDiagnostics). Zero-cost on non-mixed-size designs (no movable macros to
    // exclude), so always computed rather than gated on mixed_size_mode.
    m.final_overflow_macro_excluded = computeOverflow(false, nullptr, false, true);

    m.total_runtime = getInterval(pgrm_start_time, getTime());
    m.iteration_avg = (iteration > 0) ? m.total_runtime / iteration : 0.0f;

    // HPWL improvement, if an initial HPWL was recorded
    m.hpwl_improvement = 0.0f;
    m.has_improvement = false;
    if (m_initial_hpwl > 0) {
        m.hpwl_improvement = ((m_initial_hpwl - m.final_hpwl) / m_initial_hpwl) * 100.0f;
        m.has_improvement = true;
    }
    return m;
}

/**
 * @brief Whether the GP-stop signal counts filler density, as XPlace's overflow_fn does.
 *
 * TODO #13 re-decision (2026-08-01): unconditionally true in phase 2. The two things that
 * blocked it as a standalone phase-1 fix are specific to phase 1 and do not apply once macros
 * are frozen: phase 2 rebuilds the filler set in its own frame (so "phase 1 counts phase 2's
 * fillers" is moot), and phase 2 already runs under the un-doubled stop_overflow with the
 * plateau kill enabled (mixed_size_mode=false, Phase2.cpp:90/95) -- the same rules a
 * filler-inclusive signal was always going to need. XPlace's own std-cell-fixed-macro GP has no
 * toggle for this; it always counts fillers.
 *
 * Phase 1 keeps the config-controlled default (false) -- deciding it needs the per-design
 * clamp/no-filler vs clamp/+filler split across the full MMS suite (TODO #13 breadth item 1),
 * not yet measured.
 */
bool Placer::convergenceIncludesFillers()
{
    if (m_phase == Phase::STDCELL_FIXED_MACRO) return true;
    return cfg["params"]["convergence_include_fillers"].value_or(false);
}

/**
 * @brief DIAGNOSTIC: overflow the four ways {clamp,sharp}×{no-filler,+filler} on the
 *        restored-best placement, to reconcile our convergence metric with XPlace. XPlace's
 *        GP STOP signal is clamp+filler (overflow_fn on mov+filler density) and its reported
 *        overflow is sharp+filler. Self-contained (does not reuse FinalMetrics) so the labels
 *        stay truthful whatever filler policy the headline metrics use.
 *
 *        macro-excluded is the fifth, mixed-size-only number: sharp/no-filler with movable
 *        macros dropped from the deposit. This is the one directly comparable to
 *        tools/benchmarks.py::_XPLACE_MMS_MIXED_GP, which is evaluated after XPlace sets
 *        ps.zero_macro_grad=True (run_placement_nesterov.py:173, evaluator.py:26-45) and whose
 *        node_pos is reassembled from mov_node_pos[mov_lhs:mov_rhs] + data.node_pos[mov_rhs:]
 *        (run_placement_nesterov.py:180-181) -- i.e. filler-EXCLUDED too, not the "includes
 *        filler density" the old benchmarks.py comment claimed. On non-mixed-size designs
 *        (no movable macros) this is identical to sharp/no-filler and adds nothing.
 */
void Placer::logOverflowDiagnostics()
{
    Logger::log_detail("[OVFW-DIAG] clamp/no-filler=" + PREC(computeOverflow(true,  nullptr, false))
        + "  sharp/no-filler=" + PREC(computeOverflow(false, nullptr, false))
        + "  clamp/+filler="   + PREC(computeOverflow(true,  nullptr, true))
        + "  sharp/+filler="   + PREC(computeOverflow(false, nullptr, true))
        + "  macro-excluded="  + PREC(computeOverflow(false, nullptr, false, true))
        + "  (XPlace GP stop = clamp/+filler, XPlace report = sharp/+filler, "
        + "XPlace Mixed-GP reference = macro-excluded)");
}

/// @brief Optional: dump the restored-best bin-density map (smoothed + exact) for offline
///        comparison against XPlace. Gated by config so normal runs are unaffected.
void Placer::dumpBestPlacementDensity()
{
    if (cfg["params"]["dump_density"].value_or(false))
        dumpBinDensity((output_dir / (db.getBenchmarkName() + "_density")).string());
}

/// @brief Build and log the console results/hyperparameters tables, then export the run
///        summary and per-function timing stats as markdown into the run directory.
void Placer::exportSummaryReports(const BestSolution& chosen, const FinalMetrics& metrics,
                                   const std::string& run_output_dir)
{
    Table statistics;
    statistics.add_row({"AIEplace Run Statistics"});

    Table results;
    results.add_row({"Benchmark name", db.getBenchmarkName()});
    results.add_row(RowStream{} << "Iterations" << iteration);
    if (m_phase1_summary.valid) {
        // TODO #13 two-phase run: without this, only the phase-2 endpoint shows and the
        // macro-placement quality phase 1 is responsible for is invisible.
        results.add_row(RowStream{} << "Phase 1 Iterations" << m_phase1_summary.iterations);
        results.add_row(RowStream{} << "Phase 1 HPWL" << std::scientific << std::setprecision(3) << m_phase1_summary.hpwl);
        results.add_row(RowStream{} << "Phase 1 Overflow (smoothed)" << std::scientific << std::setprecision(3) << m_phase1_summary.overflow_smoothed);
        results.add_row(RowStream{} << "Phase 1 Overflow (exact, +fillers)" << std::scientific << std::setprecision(3) << m_phase1_summary.overflow_exact);
        results.add_row({"Phase 1 Stop reason", stopReasonName(m_phase1_summary.stop_reason)});
    }
    results.add_row(RowStream{} << "Total runtime (s)" << std::fixed << std::setprecision(3) << metrics.total_runtime);
    results.add_row(RowStream{} << "Database I/O time (s)" << std::fixed << std::setprecision(3) << Logger::getFunctionTime("setupDesign") / 1.0e6);
    results.add_row(RowStream{} << "Algorithm time (s)" << std::fixed << std::setprecision(3) << algo_time);
    results.add_row(RowStream{} << "Avg iteration time (s)" << std::fixed << std::setprecision(3) << metrics.iteration_avg);
    results.add_row(RowStream{} << "Final HPWL" << std::scientific << std::setprecision(3) << metrics.final_hpwl);
    results.add_row(RowStream{} << "Final HPWL (exact, all nets)" << std::scientific << std::setprecision(3) << metrics.final_hpwl_exact);
    if (metrics.has_improvement) {
        results.add_row(RowStream{} << "Initial HPWL" << std::scientific << std::setprecision(3) << m_initial_hpwl);
        results.add_row(RowStream{} << "HPWL improvement (%)" << std::fixed << std::setprecision(2) << metrics.hpwl_improvement);
    }
    results.add_row(RowStream{} << (convergenceIncludesFillers() ? "Final Overflow (smoothed, +fillers)"
                                                                 : "Final Overflow (smoothed, no fillers)")
                                << std::scientific << std::setprecision(3) << metrics.final_smoothed_overflow);
    results.add_row(RowStream{} << "Final Overflow (exact, +fillers)" << std::scientific << std::setprecision(3) << metrics.final_overflow);
    if (num_movable_macros > 0) {
        // Gate on num_movable_macros, not mixed_size_mode -- the latter is set false at the
        // phase-2 transition (Phase2.cpp:90), which would otherwise silently drop this row on
        // every run that actually reached phase 2.
        // Deliberately NOT prefixed "Final Overflow (exact" -- that substring is what
        // tools/{run_footprint_ab,run_mms_ab}.sh grep for the row above; a second match would
        // silently steal it (grep | tail -1 in run_footprint_ab.sh's num()).
        results.add_row(RowStream{} << "Macro-Excluded Overflow (exact, no fillers)" << std::scientific
                                    << std::setprecision(3) << metrics.final_overflow_macro_excluded);
    }
    results.add_row({"Stop reason", stopReasonName(m_stop_reason)});
    if (chosen.valid) {
        std::string type = (&chosen == &best_primary) ? "primary" : "fallback";
        std::ostringstream best_str;
        best_str << "iter " << chosen.iteration
                 << " (" << type << ", HPWL=" << std::scientific << std::setprecision(3)
                 << chosen.hpwl << ", ovfw=" << chosen.overflow << ")";
        results.add_row(RowStream{} << "Best Solution" << best_str.str());
    }
    results.column(0).format().font_align(FontAlign::right);
    results.column(1).format().font_align(FontAlign::left);

    Table hyperparams;
    hyperparams.add_row({"Hyperparameter", "Final Value"});
    hyperparams.add_row(RowStream{} << "gamma" << gamma);
    hyperparams.add_row(RowStream{} << "step_length" << step_length);
    hyperparams.add_row(RowStream{} << "bins_per_row" << bins_per_row);
    hyperparams.add_row(RowStream{} << "formula_grid" << formula_bins_per_row);
    hyperparams.add_row(RowStream{} << "num_movable_macros" << num_movable_macros);
    hyperparams.add_row(RowStream{} << "partials method" << ConfigUtils::require<std::string>(cfg, "params", "partials_compute_method"));
    hyperparams.add_row(RowStream{} << "density method" << ConfigUtils::require<std::string>(cfg, "params", "density_compute_method"));
    hyperparams.add_row(RowStream{} << "wirelength method" << ConfigUtils::require<std::string>(cfg, "params", "wirelength_method"));
    hyperparams.column(0).format().font_align(FontAlign::right);
    hyperparams.column(1).format().font_align(FontAlign::left);

    statistics.format().font_align(FontAlign::center);
    statistics.add_row({results});
    statistics.add_row({hyperparams});

    Logger::log_info(statistics);
    Logger::export_markdown(statistics, run_output_dir, "run_summary");

    Table function_stats = Logger::printFunctionStats();
    Logger::export_markdown(function_stats, run_output_dir, "function_statistics");
}

/// @brief Write the placed design to DEF, and copy the config file into the run directory for reproducibility.
void Placer::writeFinalDesignArtifacts(const std::string& run_output_dir)
{
    db.writeDEF(run_output_dir);

    std::ifstream src(m_config_filepath);
    std::ofstream dst(run_output_dir + "/config_used.toml");
    dst << src.rdbuf();
}

// Helper function to generate unique run ID
std::string Placer::generateRunId()
{
    // Simple run ID based on timestamp and benchmark
    auto now = std::chrono::high_resolution_clock::now();
    auto epoch = now.time_since_epoch();
    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch);

    std::stringstream run_id;
    run_id << db.getBenchmarkName() << "_" << nanoseconds.count() % 1000000;
    return run_id.str();
}


#ifdef __linux__
// This implementation only works on Linux systems
float Placer::getMemoryUsageMB()
{
    std::ifstream status_file("/proc/self/status");
    std::string line;

    while (std::getline(status_file, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            std::istringstream iss(line);
            std::string label, size_str, unit;
            iss >> label >> size_str >> unit;

            float size_kb = std::stof(size_str);
            return size_kb / 1024.0f;  // Convert KB to MB
        }
    }
    return 0.0f;  // Not found
}
#endif

// Additional function to track initial HPWL (call this at the start of placement)
void Placer::recordInitialHPWL()
{
    m_initial_hpwl = db.computeTotalWirelength(ConfigUtils::require<std::string>(cfg, "params", "wirelength_method"), cfg["params"]["ignore_net_degree"].value_or(100));
    Logger::log_detail("Initial HPWL recorded: " + std::to_string(m_initial_hpwl));
}

void Placer::recordIterationResults()
{
    TIME_FUNCTION();
    float hpwl = db.computeTotalWirelength(ConfigUtils::require<std::string>(cfg, "params", "wirelength_method"), cfg["params"]["ignore_net_degree"].value_or(100));
    // Drive convergence off the smoothed overflow (clamped footprints; equivalent to XPlace's
    // expand_ratio-inflated field): the smoothed density the optimizer minimizes, which descends
    // toward the stop threshold. The exact overflow is reported separately as the physical result.
    // convergence_include_fillers=true mirrors XPlace's GP-stop metric (overflow_fn), which counts
    // filler density too; default false keeps the filler-excluded (XPlace-exact) signal.
    float overflow = computeOverflow(true, nullptr, convergenceIncludesFillers()); // convergence signal

    hpwl_history.push_back(hpwl);
    step_length_history.push_back(step_length);
    ovfw_history.push_back(overflow);

    // Two-tier best solution tracking.
    // Skip early iterations to let the solver stabilize.
    if (iteration < BEST_SOL_MIN_ITER) return;

    constexpr float OVFW_EPSILON = 0.005f;

    // Primary: lowest HPWL among solutions that meet the overflow threshold (converged)
    if (overflow < overflow_threshold && hpwl < best_primary.hpwl) {
        best_primary = {hpwl, overflow, iteration, true};
        snapshotBestPlacement();
    }

    // Fallback: lowest overflow achieved. Tiebreak on HPWL when overflow is similar.
    bool overflow_clearly_improved     = (overflow < best_fallback.overflow - OVFW_EPSILON);
    bool overflow_tied_but_hpwl_better = (overflow < best_fallback.overflow + OVFW_EPSILON &&
                                          hpwl < best_fallback.hpwl);
    if (overflow_clearly_improved || overflow_tied_but_hpwl_better)
    {
        best_fallback = {hpwl, overflow, iteration, true};
        // Only snapshot if primary hasn't already saved this iteration
        if (!best_primary.valid || best_primary.iteration != iteration)
            snapshotBestPlacement();
    }
}

AIEPLACE_NAMESPACE_END
