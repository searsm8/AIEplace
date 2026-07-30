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
    if (!Logger::isKeyActive("INFO"))
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
    exportIterationVisualization(overflow);
    appendIterationLog(hpwl, overflow);
}

/// @brief Log the DSE sweep's parameter table (config output.DSE_info), if this run has one.
void Placer::printDSEInfoTable()
{
    if (!cfg["output"].contains("DSE_info")) return;

    Table DSE_info;
    std::string dse_info_str = cfg["output"]["DSE_info"].get<std::string>();
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
    int X = 10;
    if (iteration != 1 && iteration % X != 0) return;

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

/// @brief Every iterations_per_export iterations (config output), render the current placement.
///        No-op build without CREATE_VISUALIZATION.
void Placer::exportIterationVisualization(float overflow)
{
    #ifdef CREATE_VISUALIZATION
        if (!cfg["output"]["visualize"]) return;
        if (iteration > 1 && iteration % int(cfg["output"]["iterations_per_export"]) != 0) return;

        PlotInfo info = {iteration, hpwl_history.back(), overflow, step_length, density_weight, db.getBenchmarkName()};
        viz.drawPlacement(db, output_dir / "placement", info);
        //viz.drawElectricField(grid, output_dir / "efield", iteration);
    #endif
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

void Placer::plotHistories() {
#ifdef CREATE_VISUALIZATION
    fs::path graph_dir = output_dir / "graphs";
    if (!fs::exists(graph_dir))
        fs::create_directories(graph_dir);

    // Create individual plots
    CairoPlotter hpwl_plotter(800, 600);
    hpwl_plotter.plotHistory(hpwl_history, "HPWL Convergence", "HPWL Value", 0.0, 0.5, 1.0);
    hpwl_plotter.savePNG(graph_dir / "hpwl_history.png");

    CairoPlotter ovfw_plotter(800, 600);
    ovfw_plotter.plotHistory(ovfw_history, "Overflow", "OVFW Value", 0.0, 0.5, 1.0);
    ovfw_plotter.savePNG(graph_dir / "ovfw_history.png");

    CairoPlotter coeff_plotter(800, 600);
    coeff_plotter.plotHistory(step_length_history, "Step Length History", "Step Length", 1.0, 0.2, 0.2);
    coeff_plotter.savePNG(graph_dir / "step_length_history.png");

    // Create combined plot
    CairoPlotter dual_plotter(800, 600);
    dual_plotter.plotDualHistory(hpwl_history, ovfw_history,
                                "HPWL and Overflow History",
                                "HPWL (normalized)", "Overflow (normalized)");
    dual_plotter.savePNG(graph_dir / "combined_history.png");
#endif
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
                                       cfg["params"]["partials_compute_method"].get<std::string>() + "_" +
                                       cfg["params"]["density_compute_method"].get<std::string>());

    fs::create_directories(output_dir);

    Logger::log_info("Created output directory: " + output_dir.string());
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
    if (!cfg["output"].contains("DSE_info"))
        return dse_params;

    std::string dse_str = cfg["output"]["DSE_info"].get<std::string>();
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

void Placer::printFinalResults()
{
    Logger::log_info("AIEplace algorithm complete.");

    BestSolution& chosen = restoreBestSolution();

    // Use the output directory created in constructor
    std::string run_output_dir = output_dir.string();
    std::string run_id = generateRunId();

    FinalMetrics metrics = computeFinalMetrics();
    logOverflowDiagnostics(metrics);
    dumpBestPlacementDensity();

    exportSummaryReports(chosen, metrics, run_output_dir);

    // Write run record to global results CSV
    writeResultsCSV(metrics.final_hpwl, metrics.final_hpwl_exact, metrics.final_overflow,
                    metrics.total_runtime, metrics.iteration_avg, metrics.hpwl_improvement, run_id);

    exportVisualizationArtifacts(chosen, metrics, run_output_dir);
    writeFinalDesignArtifacts(run_output_dir);

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
    m.final_hpwl = db.computeTotalWirelength(cfg["params"]["wirelength_method"], cfg["params"].value("ignore_net_degree", 100));
    // Exact HPWL over ALL nets (no net-degree mask). The masked final_hpwl above drops nets with
    // > ignore_net_degree pins (matching XPlace's GP metric); this includes them, matching XPlace's
    // post-GP "exact HPWL" / published-number convention (get_obj_hpwl is unmasked). Report both:
    // masked for the schedule + GP-vs-GP, exact for the apples-to-apples vs XPlace's headline HPWL.
    // A huge cap (not -1, which would exclude every net) includes every degree.
    m.final_hpwl_exact = db.computeTotalWirelength(cfg["params"]["wirelength_method"], 1000000000);
    // Exact (physical) overflow — sharp footprints, the real spreading quality. Reported
    // alongside the smoothed overflow that drove convergence (see computeOverflow).
    m.final_overflow = computeOverflow(false);
    m.final_smoothed_overflow = computeOverflow(true);

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
 *        GP STOP signal is clamp+filler (overflow_fn on mov+filler density); our convergence
 *        signal is clamp,no-filler (= XPlace's exact eval). Measures whether including fillers
 *        reproduces XPlace's lower stop overflow. Temporary — remove once the convergence
 *        metric is reconciled.
 */
void Placer::logOverflowDiagnostics(const FinalMetrics& metrics)
{
    float ovf_clamp_filler = computeOverflow(true,  nullptr, true);
    float ovf_sharp_filler = computeOverflow(false, nullptr, true);
    Logger::log_info("[OVFW-DIAG] clamp/no-filler=" + PREC(metrics.final_smoothed_overflow)
        + "  sharp/no-filler=" + PREC(metrics.final_overflow)
        + "  clamp/+filler=" + PREC(ovf_clamp_filler)
        + "  sharp/+filler=" + PREC(ovf_sharp_filler)
        + "  (XPlace GP stop = clamp/+filler)");
}

/// @brief Optional: dump the restored-best bin-density map (smoothed + exact) for offline
///        comparison against XPlace. Gated by config so normal runs are unaffected.
void Placer::dumpBestPlacementDensity()
{
    if (cfg["params"].contains("dump_density") && cfg["params"]["dump_density"])
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
    results.add_row(RowStream{} << "Final Overflow (smoothed)" << std::scientific << std::setprecision(3) << metrics.final_smoothed_overflow);
    results.add_row(RowStream{} << "Final Overflow (exact)" << std::scientific << std::setprecision(3) << metrics.final_overflow);
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
    hyperparams.add_row(RowStream{} << "partials method" << cfg["params"]["partials_compute_method"]);
    hyperparams.add_row(RowStream{} << "density method" << cfg["params"]["density_compute_method"]);
    hyperparams.add_row(RowStream{} << "wirelength method" << cfg["params"]["wirelength_method"]);
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

/// @brief Render the restored-best placement and (if visualization is built) assemble the run's placement GIF.
void Placer::exportVisualizationArtifacts(const BestSolution& chosen, const FinalMetrics& metrics,
                                           const std::string& run_output_dir)
{
    #ifdef CREATE_VISUALIZATION
        if (!cfg["output"]["visualize"]) return;

        PlotInfo info;
        if (chosen.valid) {
            info = {chosen.iteration, chosen.hpwl, chosen.overflow, 0, 0,
                    db.getBenchmarkName(), "best_solution"};
        } else {
            info = {iteration, metrics.final_hpwl, metrics.final_overflow, step_length, density_weight,
                    db.getBenchmarkName(), "best_solution"};
        }
        viz.drawPlacement(db, run_output_dir, info);

        // use python script to create gif from generated pngs in run directory
        std::string quiet_flag = quiet ? " --quiet" : "";
        std::string gif_command = "python3 tools/gif_builder.py " + run_output_dir + "/placement" + " -d 100 -o " + run_output_dir + "/full_placement.gif" + quiet_flag;
        if (system(gif_command.c_str()) != 0)
            Logger::log_warning("gif_builder.py failed (non-fatal): " + gif_command);
    #endif
}

/// @brief Write the placed design to DEF, and copy the config file into the run directory for reproducibility.
void Placer::writeFinalDesignArtifacts(const std::string& run_output_dir)
{
    db.writeDEF(run_output_dir);

    std::ifstream src(m_config_filepath);
    std::ofstream dst(run_output_dir + "/config_used.json");
    dst << src.rdbuf();
}

// Helper function to escape JSON strings
std::string Placer::escapeJsonString(const std::string& input)
{
    std::string output;
    output.reserve(input.length() + 10); // Reserve some extra space for escapes

    for (char c : input) {
        switch (c) {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b";  break;
            case '\f': output += "\\f";  break;
            case '\n': output += "\\n";  break;
            case '\r': output += "\\r";  break;
            case '\t': output += "\\t";  break;
            default:
                if (c < 0x20) {
                    output += "\\u";
                    output += "0000";
                    // Simple hex conversion for control characters
                    char hex[3];
                    sprintf(hex, "%02x", (unsigned char)c);
                    output[output.length() - 2] = hex[0];
                    output[output.length() - 1] = hex[1];
                } else {
                    output += c;
                }
                break;
        }
    }
    return output;
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
    m_initial_hpwl = db.computeTotalWirelength(cfg["params"]["wirelength_method"], cfg["params"].value("ignore_net_degree", 100));
    Logger::log_info("Initial HPWL recorded: " + std::to_string(m_initial_hpwl));
}

void Placer::initializeFocus()
{
    addNamedFocusNets();

    std::mt19937 rng(std::random_device{}());
    addRandomFocusNets(rng);
    addRandomFocusNodes(rng);
    addRandomMacroNets(rng);
    addRandomFocusIO(rng);
}

/// @brief Add nets named in config output.focus_nets to the visualization focus set.
void Placer::addNamedFocusNets()
{
    if (!cfg["output"].contains("focus_nets")) return;

    auto& nets = db.getNets();
    for (const auto& name : cfg["output"]["focus_nets"]) {
        string net_name = name.get<string>();
        auto it = nets.find(net_name);
        if (it != nets.end()) {
            db.addFocusNet(it->second);
            Logger::log_info("Focus net (named): " + net_name
                + " (" + std::to_string(it->second->getNodes().size()) + " nodes)");
        } else {
            Logger::log_warning("Focus net not found: " + net_name);
        }
    }
}

/// @brief Add a random sample of nets (config output.rand_focus_nets) to the visualization focus set.
void Placer::addRandomFocusNets(std::mt19937& rng)
{
    int num_focus_nets = cfg["output"].value("rand_focus_nets", 0);
    if (num_focus_nets <= 0) return;

    auto& nets = db.getNets();
    std::vector<Net*> all_nets;
    all_nets.reserve(nets.size());
    for (auto& [id, net_p] : nets)
        all_nets.push_back(net_p);
    std::shuffle(all_nets.begin(), all_nets.end(), rng);
    int count = std::min(num_focus_nets, (int)all_nets.size());
    for (int i = 0; i < count; i++) {
        db.addFocusNet(all_nets[i]);
        Logger::log_info("Focus net (rand) " + std::to_string(i) + ": " + all_nets[i]->getName()
            + " (" + std::to_string(all_nets[i]->getNodes().size()) + " nodes)");
    }
}

/// @brief Add a random sample of movable nodes (config output.rand_focus_nodes) to the visualization focus set.
void Placer::addRandomFocusNodes(std::mt19937& rng)
{
    int num_focus_nodes = cfg["output"].value("rand_focus_nodes", 0);
    if (num_focus_nodes <= 0) return;

    std::vector<Node*> movable;
    for (auto& [name, comp_p] : db.getComponents())
        if (comp_p->getStatus() != FIXED)
            movable.push_back(comp_p);
    std::shuffle(movable.begin(), movable.end(), rng);
    int count = std::min(num_focus_nodes, (int)movable.size());
    for (int i = 0; i < count; i++) {
        db.addFocusNode(movable[i]);
        Logger::log_info("Focus node (rand) " + std::to_string(i) + ": " + movable[i]->getName()
            + " pos=(" + std::to_string(movable[i]->getPos().x) + ","
            + std::to_string(movable[i]->getPos().y) + ")");
    }
}

/// @brief Add a random sample of nets with a pin on a fixed macro (config output.rand_macro_nets,
///        Component only, not IOPad) to the visualization focus set.
void Placer::addRandomMacroNets(std::mt19937& rng)
{
    int num_macro_nets = cfg["output"].value("rand_macro_nets", 0);
    if (num_macro_nets <= 0) return;

    auto& nets = db.getNets();
    std::vector<Net*> macro_nets;
    for (auto& [id, net_p] : nets) {
        for (Node* node_p : net_p->getNodes()) {
            if (dynamic_cast<Component*>(node_p) && node_p->getStatus() == FIXED) {
                macro_nets.push_back(net_p);
                break;
            }
        }
    }
    std::shuffle(macro_nets.begin(), macro_nets.end(), rng);
    int count = std::min(num_macro_nets, (int)macro_nets.size());
    for (int i = 0; i < count; i++) {
        db.addFocusNet(macro_nets[i]);
        Logger::log_info("Focus net (macro) " + std::to_string(i) + ": " + macro_nets[i]->getName()
            + " (" + std::to_string(macro_nets[i]->getNodes().size()) + " nodes)");
    }
}

/// @brief Add a random sample of IOPads/fixed components (config output.rand_focus_IO) to the visualization focus set.
void Placer::addRandomFocusIO(std::mt19937& rng)
{
    int num_focus_io = cfg["output"].value("rand_focus_IO", 0);
    if (num_focus_io <= 0) return;

    std::vector<Node*> fixed_nodes;
    for (auto& [name, pad] : db.getIOPads())
        fixed_nodes.push_back(pad);
    for (auto& [name, comp_p] : db.getComponents())
        if (comp_p->getStatus() == FIXED)
            fixed_nodes.push_back(comp_p);
    std::shuffle(fixed_nodes.begin(), fixed_nodes.end(), rng);
    int count = std::min(num_focus_io, (int)fixed_nodes.size());
    for (int i = 0; i < count; i++) {
        db.addFocusNode(fixed_nodes[i]);
        Logger::log_info("Focus IO (rand) " + std::to_string(i) + ": " + fixed_nodes[i]->getName()
            + " pos=(" + std::to_string(fixed_nodes[i]->getPos().x) + ","
            + std::to_string(fixed_nodes[i]->getPos().y) + ")");
    }
}

void Placer::recordIterationResults()
{
    float hpwl = db.computeTotalWirelength(cfg["params"]["wirelength_method"], cfg["params"].value("ignore_net_degree", 100));
    // Drive convergence off the smoothed overflow (clamped footprints; equivalent to XPlace's
    // expand_ratio-inflated field): the smoothed density the optimizer minimizes, which descends
    // toward the stop threshold. The exact overflow is reported separately as the physical result.
    // convergence_include_fillers=true mirrors XPlace's GP-stop metric (overflow_fn), which counts
    // filler density too; default false keeps the filler-excluded (XPlace-exact) signal.
    bool conv_incl_fillers = cfg["params"].value("convergence_include_fillers", false);
    float overflow = computeOverflow(true, nullptr, conv_incl_fillers);   // convergence signal

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
