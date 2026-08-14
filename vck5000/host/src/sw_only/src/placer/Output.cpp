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
                              float total_runtime,
                              float hpwl_improvement, const std::string& run_id)
{
    if (!fs::exists(results_dir))
        fs::create_directories(results_dir);

    fs::path csv_path = results_dir / "results.csv";
    bool need_header = !fs::exists(csv_path);

    std::ofstream out_file;
    out_file.open(csv_path, std::ios_base::app);
    out_file.imbue(std::locale::classic());  // Prevent comma thousands separators

    float xplace_ref = lookupXplaceReferenceHPWL(db.getBenchmarkPath());  // suite/design -- bare name collides across suites
    std::vector<std::pair<std::string, std::string>> dse_params = parseDSEParams();

    if (need_header)
        writeResultsCSVHeader(out_file, dse_params);

    writeResultsCSVRow(out_file, final_hpwl_exact, total_runtime, dse_params, xplace_ref);

    out_file.close();
}

/**
 * @brief XPlace GP-ONLY reference HPWL (masked_hpwl at "GP Stop", NOT legalized), so the Ratio
 *        column is an honest GP-vs-GP comparison instead of our-GP vs XPlace-GP+legalization.
 *        Values from local XPlace runs: ~/phd/Xplace/result/<ts>_<design>/log/test.log line
 *        "GP Stop! ... masked_hpwl: X".
 *
 * ### KEYED ON "<suite>/<design>", NOT the bare design name (fixed 2026-08-07)
 * adaptec1-4 and bigblue1-4 exist in BOTH `ispd2005` and `mms`, and their references differ by
 * ~15% (ispd2005/adaptec1 7.060e7 vs mms/adaptec1 6.453e7). Keyed on the bare name this returned
 * the ISPD2005 number for an MMS run, silently, and the Ratio column of every MMS results.csv
 * row was wrong by that much. `tools/xplace_gp_ref.py` had the same defect.
 *
 * Only ISPD2005 is populated: XPlace's ispd2005 HPWL shares sw_only's raw-DBU frame. ISPD2015
 * (mgc_*) XPlace HPWL is site-width-normalized (~ /site_width, e.g. /200) -- a DIFFERENT frame --
 * so it must NOT be mixed in here; populate mgc as masked_hpwl*site_width once measured.
 * ispd2005 bigblue3/bigblue4 still need a local XPlace GP run.
 *
 * NOTE this is a second copy of data that also lives in `tools/benchmarks.py` (which carries the
 * richer post-GP/LG/DP reference, but only for MMS). Two tables that can disagree is exactly the
 * pattern this fix was cleaning up; collapsing them is worth doing once benchmarks.py covers all
 * three suites.
 * @return the reference HPWL, or 0.0f if this benchmark has no recorded reference.
 */
float Placer::lookupXplaceReferenceHPWL(const std::string& bench_path)
{
    static const std::map<std::string, float> xplace_hpwl = {
        {"ispd2005/adaptec1",  7.060218e+07f},
        {"ispd2005/adaptec2",  7.893496e+07f},
        {"ispd2005/adaptec3",  1.858436e+08f},
        {"ispd2005/adaptec4",  1.675808e+08f},
        {"ispd2005/bigblue1",  8.721903e+07f},
        {"ispd2005/bigblue2",  1.298895e+08f},
    };
    auto it = xplace_hpwl.find(bench_path);
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
    out_file << "Total Runtime (sec),";
    out_file << "Memory Usage (MB),";
    out_file << "Output Dir,";
    out_file << "Timestamp";
    out_file << endl;
}

/// @brief Write one results.csv data row for this run.
void Placer::writeResultsCSVRow(std::ofstream& out_file, float final_hpwl_exact, float total_runtime,
                                 const std::vector<std::pair<std::string, std::string>>& dse_params,
                                 float xplace_ref)
{
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream timestamp;
    timestamp << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");

    out_file << "\"" << db.getBenchmarkName() << "\",";
    out_file << iteration << ",";
    // Best solution info -- the same rule that picked the shipped placement, or N/A
    const BestChoice best = selectBestSolution();
    if (best.sol) {
        out_file << best.sol->iteration << ","
                 << std::scientific << PREC(best.sol->overflow) << ","
                 << std::scientific << SCI(best.sol->hpwl) << ",";
        // XPlace reference and ratio
        if (xplace_ref > 0.0f) {
            out_file << std::scientific << SCI(xplace_ref) << ","
                     << std::fixed << std::setprecision(2) << (best.sol->hpwl / xplace_ref) << ",";
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
    out_file << std::fixed << std::setprecision(3);
    out_file << total_runtime << ",";
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

    BestChoice chosen = restoreBestSolution();

    // Use the output directory created in constructor
    std::string run_output_dir = output_dir.string();
    std::string run_id = generateRunId();

    FinalMetrics metrics = computeFinalMetrics();
    logOverflowDiagnostics();
    dumpBestPlacementDensity();

    exportSummaryReports(chosen, metrics, run_output_dir);

    // Write run record to global results CSV
    writeResultsCSV(metrics.final_hpwl, metrics.final_hpwl_exact, metrics.final_overflow,
                    metrics.total_runtime, metrics.hpwl_improvement, run_id);

    // The restored best placement, tagged so the offline tool can render the same picture the
    // cairo renderer writes as best_solution.png. That shared final frame is what step 2's
    // C++-vs-Python comparison is anchored on (handoff §6).
    dumpIterationPositions("best_solution");

    writeFinalDesignArtifacts(run_output_dir);
    finalizePositionDump();

    Logger::log_info("All outputs saved to: " + run_output_dir);
}

/// @brief Apply selectBestSolution() and restore THAT tracker's geometry. The slot comes from the
///        same struct as the metadata being logged, so the two cannot disagree (TODO #24).
Placer::BestChoice Placer::restoreBestSolution()
{
    BestChoice chosen = selectBestSolution();

    if (chosen.sol) {
        restoreBestPlacement(chosen.slot);
        syncProbeToCommitted();   // everything below this point reports on the restored placement
        Logger::log_info("Restored " + std::string(chosen.type) + " best placement from iteration " +
            std::to_string(chosen.sol->iteration) +
            " (HPWL: " + std::to_string(chosen.sol->hpwl) +
            ", overflow: " + std::to_string(chosen.sol->overflow) + ")");
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
    // EXCLUDED, matching XPlace's reported overflow (evaluate_placement -> get_obj_overflow,
    // evaluator.py:26-50: movable slice `[mov_lhs:mov_rhs]` only, denominator
    // `total_mov_area_without_filler`). CORRECTED 2026-08-06 — this was `true` from 2026-07-31
    // on the belief that XPlace counts fillers here; it does not, and the +filler number reads
    // roughly 2x high against anything XPlace prints.
    m.final_overflow = computeOverflow(false, nullptr, false);
    // The smoothed overflow the run actually converged on — same filler policy as the
    // convergence signal (recordIterationResults), so the report explains why it stopped.
    m.final_smoothed_overflow = computeOverflow(true, nullptr, false);
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
 * @brief DIAGNOSTIC: overflow the four ways {clamp,sharp}×{no-filler,+filler} on the
 *        restored-best placement, to reconcile our convergence metric with XPlace. XPlace's
 *        GP STOP signal is clamp/NO-filler and its reported "exact Overflow" is sharp/NO-filler
 *        -- both slice the movable range `[mov_lhs:mov_rhs]`, which excludes fillers; see
 *        the recordIterationResults note for the three XPlace call sites. (This used to say
 *        clamp/+filler and sharp/+filler respectively; corrected 2026-08-06.) Self-contained
 *        (does not reuse FinalMetrics) so the labels stay truthful whatever filler policy the
 *        headline metrics use.
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
        + "  (XPlace GP stop = clamp/no-filler, XPlace report = sharp/no-filler, "
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
void Placer::exportSummaryReports(const BestChoice& chosen, const FinalMetrics& metrics,
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
        results.add_row(RowStream{} << "Phase 1 Overflow (exact, no fillers)" << std::scientific << std::setprecision(3) << m_phase1_summary.overflow_exact);
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
    results.add_row(RowStream{} << "Final Overflow (smoothed, no fillers)"
                                << std::scientific << std::setprecision(3) << metrics.final_smoothed_overflow);
    results.add_row(RowStream{} << "Final Overflow (exact, no fillers)" << std::scientific << std::setprecision(3) << metrics.final_overflow);
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
    if (chosen.sol) {
        std::ostringstream best_str;
        best_str << "iter " << chosen.sol->iteration
                 << " (" << chosen.type << ", HPWL=" << std::scientific << std::setprecision(3)
                 << chosen.sol->hpwl << ", ovfw=" << chosen.sol->overflow << ")";
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
    // Fillers are EXCLUDED, as XPlace's overflow_fn excludes them: it runs on `mov_density_map`,
    // the movable-only slice `[mov_lhs:mov_rhs]`, and `filler_density_map` (`[mov_rhs:]`) is added
    // only afterwards, and only for the FORCE (electronic_density_layer.py:36-50, 272-292;
    // fillers are appended past mov_rhs by get_mov_node_info, database.py:901-904). The reported
    // "exact Overflow" excludes them too (get_obj_overflow, evaluator.py:26-50).
    // A `convergence_include_fillers` toggle briefly forced this TRUE in phase 2 (2026-08-02) on
    // the opposite belief; retracted and deleted 2026-08-07 after the 16-design A/B — TODO #19a.
    float overflow = computeOverflow(true, nullptr, false); // convergence signal

    hpwl_history.push_back(hpwl);
    step_length_history.push_back(step_length);
    ovfw_history.push_back(overflow);

    // Best-solution tracking, ported from XPlace's update_best_sol (param_scheduler.py:390-451).
    // Skip early iterations to let the solver stabilize (XPlace: `iter - init_iter < 50`).
    if (iteration < BEST_SOL_MIN_ITER) return;

    const bool converged_now = (overflow < overflow_threshold);

    // XPlace frees the rollback net on the FIRST converged iteration (param_scheduler.py:396-405).
    // That lifetime is what lets the selection rule give rollback absolute priority: if it is still
    // around, the run never converged and there is nothing else to choose. Once dropped it stays
    // dropped, even if overflow later climbs back above the threshold.
    if (converged_now && !ever_converged) {
        ever_converged = true;
        best_rollback  = BestSolution{};
    }

    // Rollback: near-converged band, before any converged solution exists. Overflow must improve;
    // HPWL is allowed to creep 1% to buy it (param_scheduler.py:407-428).
    if (!ever_converged && overflow < 5.0f * overflow_threshold &&
        hpwl < best_rollback.hpwl * 1.01f && overflow < best_rollback.overflow)
    {
        best_rollback = {hpwl, overflow, iteration, true};
        snapshotBestPlacement(BestSlot::ROLLBACK);
    }

    // Aux: converged, driving overflow down, paying at most 0.5% HPWL per update (:431-441).
    // This is NOT a divergence guard -- it is the spread-out solution the selection rule PREFERS.
    if (converged_now && hpwl < best_aux.hpwl * 1.005f && overflow < best_aux.overflow) {
        best_aux = {hpwl, overflow, iteration, true};
        snapshotBestPlacement(BestSlot::AUX);
    }

    // Primary: converged, lowest HPWL (:444-450).
    if (converged_now && hpwl < best_primary.hpwl) {
        best_primary = {hpwl, overflow, iteration, true};
        snapshotBestPlacement(BestSlot::PRIMARY);
    }
}

AIEPLACE_NAMESPACE_END
