// Output.cpp
// Output, reporting, and results management functions
// Separated from AIEplace.cpp for better organization

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
    // only print if NOT in quiet mode
    if(!quiet)
    {
        //system("clear"); // Clear console for cleaner output each iteration
        //printWelcomeBanner(false);
    }

    if (cfg["output"].contains("DSE_info"))
    {
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

    // every X iterations, export a table in markdown
    int X = 1;
    float hpwl = hpwl_history.back();
    float overflow = ovfw_history.back();
    if (iteration % X == 0)
    {
        Table top;
        top.add_row(RowStream{} << "   " << "Benchmark" << db.getBenchmarkName() << "    ");
        top.add_row(RowStream{} << "   " << "Iteration" << iteration << "   ");
        top.add_row(RowStream{} << "   " << "HPWL" << SCI(hpwl) << "   ");
        top.add_row(RowStream{} << "   " << "Overflow" << PREC(overflow) << "   ");
        top.add_row(RowStream{} << "   " << "Step Length" << SCI(step_length) << "   ");
        top.add_row(RowStream{} << "   " << "Density Weight" << SCI(density_weight) << "   ");
        top.add_row(RowStream{} << "   " << "BkTrk steps" << std::to_string(backtrack_steps) << "   ");
        top.column(1).format().font_align(FontAlign::right);
        top.column(2).format().font_align(FontAlign::left);
        //top.format().hide_border();
        Logger::log_data(top);
    }

    // every 10 iterations, export an image
    #ifdef CREATE_VISUALIZATION
        if(cfg["output"]["visualize"])
        if (iteration < 10 || iteration % int(cfg["output"]["iterations_per_export"]) == 0) {
            PlotInfo info = {iteration, hpwl_history.back(), overflow, step_length, density_weight, db.getBenchmarkName()};
            viz.drawPlacement(db, output_dir / "placement", info);
            //viz.drawElectricField(grid, output_dir / "efield", iteration);
        }
    #endif

    //Logger::export_eField(grid, output_dir, iteration);

    // Append the HPWL value to a file for later analysis
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

// Append one row of the schedule's per-iteration inputs and outputs to schedule_trace.csv.
// This is the golden trace the PL param_scheduler port is verified against (offline, no device):
// given the INPUTS a scheduler consumes at iteration k (hpwl, overflow, the two BB norms,
// density_force_fraction) plus its persistent state, it must reproduce the OUTPUTS this row
// records (gamma/inv_gamma, step_length=alpha, momentum_coeff, density_weight=lambda). Written
// at the END of performIteration, so gamma/density_weight already hold the NEXT iteration's values.
void Placer::dumpScheduleTrace() {
    std::ofstream f;
    fs::path path = output_dir;
    f.open(path.append("schedule_trace.csv"), std::ios_base::app);
    if (iteration == 1)
        f << "iter,hpwl,overflow,pos_norm_sq,grad_norm_sq,density_force_fraction,"
             "base_gamma,gamma,inv_gamma,step_length,nesterov_ak,momentum_coeff,density_weight,"
             "precond_coef,precond_a1_norm,precond_a2_norm\n";
    f << std::scientific << std::setprecision(9)
      << iteration << ','
      << hpwl_history.back()    << ',' << ovfw_history.back()   << ','
      << last_pos_norm_sq       << ',' << last_grad_norm_sq     << ','
      << density_force_fraction << ',' << base_gamma            << ','
      << gamma                  << ',' << inv_gamma             << ','
      << step_length            << ',' << nesterov_ak           << ','
      << momentum_coeff         << ',' << density_weight        << ','
      << precond_coef           << ',' << precond_a1_norm       << ','
      << precond_a2_norm        << '\n';
    f.close();
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

    std::ofstream out_file;
    bool need_header = !fs::exists(csv_path);
    out_file.open(csv_path, std::ios_base::app);
    out_file.imbue(std::locale::classic());  // Prevent comma thousands separators

    // XPlace GP-ONLY reference HPWL (masked_hpwl at "GP Stop", NOT legalized), so the Ratio is an
    // honest GP-vs-GP comparison instead of our-GP vs XPlace-GP+legalization. Values from local
    // XPlace runs: ~/phd/Xplace/result/<ts>_<design>/log/test.log line "GP Stop! ... masked_hpwl: X".
    // Only ISPD2005 is populated: XPlace's ispd2005 HPWL shares sw_only's raw-DBU frame. ISPD2015
    // (mgc_*) XPlace HPWL is site-width-normalized (~ /site_width, e.g. /200) -- a DIFFERENT frame --
    // so it must NOT be mixed in here; populate mgc as masked_hpwl*site_width once measured. bigblue3/
    // bigblue4 need a local XPlace GP run. Designs absent here -> XPlace GP HPWL + Ratio print N/A.
    static const std::map<std::string, float> xplace_hpwl = {
        {"adaptec1",  7.060218e+07f},
        {"adaptec2",  7.893496e+07f},
        {"adaptec3",  1.858436e+08f},
        {"adaptec4",  1.675808e+08f},
        {"bigblue1",  8.721903e+07f},
        {"bigblue2",  1.298895e+08f},
    };

    // Lookup XPlace reference for this benchmark
    std::string bench_name = db.getBenchmarkName();
    float xplace_ref = 0.0f;
    auto it = xplace_hpwl.find(bench_name);
    if (it != xplace_hpwl.end())
        xplace_ref = it->second;

    // Parse DSE sweep parameters into individual key=value pairs
    // Skip first two lines (progress counter and benchmark name — redundant with other columns)
    std::vector<std::pair<std::string, std::string>> dse_params;
    if (cfg["output"].contains("DSE_info")) {
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
    }

    if (need_header) {
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
        out_file << "Partials AIE Time (sec),";
        out_file << "Memory Usage (MB),";
        out_file << "Output Dir,";
        out_file << "Timestamp,";
        out_file << "HPWL_Graph,";
        out_file << "Combined_Graph,";
        out_file << "Placement_GIF";
        out_file << endl;
    }

    // Graph hyperlinks (only populated when visualization is built)
    std::string hpwl_graph_cell, combined_graph_cell, placement_gif_cell;
#ifdef CREATE_VISUALIZATION
    // Strip results_dir prefix so hyperlinks are relative to the CSV's location
    std::string out_str = output_dir.string();
    std::string results_prefix = results_dir.string() + "/";
    std::string rel_str = (out_str.rfind(results_prefix, 0) == 0) ? out_str.substr(results_prefix.size()) : out_str;
    std::string hpwl_path    = rel_str + "/graphs/hpwl_history.png";
    std::string combined_path = rel_str + "/graphs/combined_history.png";
    std::string gif_path      = rel_str + "/full_placement.gif";
    hpwl_graph_cell     = "\"=HYPERLINK(\"\"" + hpwl_path     + "\"\",\"\"view\"\")\"";
    combined_graph_cell = "\"=HYPERLINK(\"\"" + combined_path + "\"\",\"\"view\"\")\"";
    placement_gif_cell  = "\"=HYPERLINK(\"\"" + gif_path      + "\"\",\"\"view\"\")\"";
#endif

    // Timestamp
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
    out_file << db_IO_time << ",";
    out_file << algo_time << ",";
    out_file << iteration_avg << ",";
    out_file << Logger::getFunctionTime("computeAllPartials_AIE") << ",";
    out_file << getMemoryUsageMB() << ",";
    out_file << "\"" << output_dir.string() << "\",";
    out_file << "\"" << timestamp.str() << "\",";
    out_file << (hpwl_graph_cell.empty() ? "none" : hpwl_graph_cell) << ",";
    out_file << (combined_graph_cell.empty() ? "none" : combined_graph_cell) << ",";
    out_file << (placement_gif_cell.empty() ? "none" : placement_gif_cell);
    out_file << endl;
    out_file.close();
}

// Enhanced printFinalResults with organized output structure
void Placer::printFinalResults()
{
    Logger::log_info("AIEplace algorithm complete.");

    // Select best solution: primary (HPWL-driven, converged) > fallback (Pareto) > last
    BestSolution& chosen = best_primary.valid ? best_primary
                         : best_fallback.valid ? best_fallback
                         : best_primary; // will be invalid, restoreBest is a no-op

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

    // Use the output directory created in constructor
    std::string run_output_dir = output_dir.string();
    std::string run_id = generateRunId();

    // Calculate final metrics
    algo_time = Logger::getFunctionTime("run")/ 1e6; // Convert microseconds to seconds
    float final_hpwl = db.computeTotalWirelength(cfg["params"]["wirelength_method"], cfg["params"].value("ignore_net_degree", 100));
    // Exact HPWL over ALL nets (no net-degree mask). The masked final_hpwl above drops nets with
    // > ignore_net_degree pins (matching XPlace's GP metric); this includes them, matching XPlace's
    // post-GP "exact HPWL" / published-number convention (get_obj_hpwl is unmasked). Report both:
    // masked for the schedule + GP-vs-GP, exact for the apples-to-apples vs XPlace's headline HPWL.
    // A huge cap (not -1, which would exclude every net) includes every degree.
    float final_hpwl_exact = db.computeTotalWirelength(cfg["params"]["wirelength_method"], 1000000000);
    // Exact (physical) overflow — sharp footprints, the real spreading quality. Reported
    // alongside the smoothed overflow that drove convergence (see computeOverflow).
    float final_overflow = computeOverflow(false);
    float final_smoothed_overflow = computeOverflow(true);

    // DIAGNOSTIC: overflow the four ways {clamp,sharp}×{no-filler,+filler} on the restored-best
    // placement, to reconcile our convergence metric with XPlace. XPlace's GP STOP signal is
    // clamp+filler (overflow_fn on mov+filler density); our convergence signal is clamp,no-filler
    // (= XPlace's exact eval). Measures whether including fillers reproduces XPlace's lower stop
    // overflow. Temporary — remove once the convergence metric is reconciled.
    float ovf_clamp_filler = computeOverflow(true,  nullptr, true);
    float ovf_sharp_filler = computeOverflow(false, nullptr, true);
    Logger::log_info("[OVFW-DIAG] clamp/no-filler=" + PREC(final_smoothed_overflow)
        + "  sharp/no-filler=" + PREC(final_overflow)
        + "  clamp/+filler=" + PREC(ovf_clamp_filler)
        + "  sharp/+filler=" + PREC(ovf_sharp_filler)
        + "  (XPlace GP stop = clamp/+filler)");

    // Optional: dump the restored-best bin-density map (smoothed + exact) for offline
    // comparison against XPlace. Gated by config so normal runs are unaffected.
    if (cfg["params"].contains("dump_density") && cfg["params"]["dump_density"]) {
        dumpBinDensity((output_dir / (db.getBenchmarkName() + "_density")).string());
    }

    float total_runtime = getInterval(pgrm_start_time, getTime());
    float iteration_avg = (iteration > 0) ? total_runtime / iteration : 0.0f;

    // Calculate HPWL improvement if initial HPWL was recorded
    float hpwl_improvement = 0.0f;
    bool has_improvement = false;
    if (m_initial_hpwl > 0) {
        hpwl_improvement = ((m_initial_hpwl - final_hpwl) / m_initial_hpwl) * 100.0f;
        has_improvement = true;
    }

    // Traditional table output (existing functionality) - still to console
    Table statistics;
    statistics.add_row({"AIEplace Run Statistics"});

    Table results;
    results.add_row({"Benchmark name", db.getBenchmarkName()});
    results.add_row(RowStream{} << "Iterations" << iteration);
    results.add_row(RowStream{} << "Total runtime (s)" << std::fixed << std::setprecision(3) << total_runtime);
    results.add_row(RowStream{} << "Database I/O time (s)" << std::fixed << std::setprecision(3) << db_IO_time);
    results.add_row(RowStream{} << "Algorithm time (s)" << std::fixed << std::setprecision(3) << algo_time);
    results.add_row(RowStream{} << "Avg iteration time (s)" << std::fixed << std::setprecision(3) << iteration_avg);
    results.add_row(RowStream{} << "Final HPWL" << std::scientific << std::setprecision(3) << final_hpwl);
    results.add_row(RowStream{} << "Final HPWL (exact, all nets)" << std::scientific << std::setprecision(3) << final_hpwl_exact);
    if (has_improvement) {
        results.add_row(RowStream{} << "Initial HPWL" << std::scientific << std::setprecision(3) << m_initial_hpwl);
        results.add_row(RowStream{} << "HPWL improvement (%)" << std::fixed << std::setprecision(2) << hpwl_improvement);
    }
    results.add_row(RowStream{} << "Final Overflow (smoothed)" << std::scientific << std::setprecision(3) << final_smoothed_overflow);
    results.add_row(RowStream{} << "Final Overflow (exact)" << std::scientific << std::setprecision(3) << final_overflow);
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
    hyperparams.add_row(RowStream{} << "partials method" << cfg["params"]["partials_compute_method"]);
    hyperparams.add_row(RowStream{} << "density method" << cfg["params"]["density_compute_method"]);
    hyperparams.add_row(RowStream{} << "wirelength method" << cfg["params"]["wirelength_method"]);
    hyperparams.column(0).format().font_align(FontAlign::right);
    hyperparams.column(1).format().font_align(FontAlign::left);

    statistics.format().font_align(FontAlign::center);
    statistics.add_row({results});
    statistics.add_row({hyperparams});

    Logger::log_data(statistics);

    // Export markdown to run-specific directory
    Logger::export_markdown(statistics, run_output_dir, "run_summary");

    // Function statistics to run-specific directory
    Table function_stats = Logger::printFunctionStats();
    Logger::export_markdown(function_stats, run_output_dir, "function_statistics");

    // Write run record to global results CSV
    writeResultsCSV(final_hpwl, final_hpwl_exact, final_overflow, total_runtime,
                    iteration_avg, hpwl_improvement, run_id);

    // Generate visualization in run-specific directory
    #ifdef CREATE_VISUALIZATION
        if(cfg["output"]["visualize"]) {
            PlotInfo info;
            if (chosen.valid) {
                info = {chosen.iteration, chosen.hpwl, chosen.overflow, 0, 0,
                        db.getBenchmarkName(), "best_solution"};
            } else {
                info = {iteration, final_hpwl, final_overflow, step_length, density_weight,
                        db.getBenchmarkName(), "best_solution"};
            }
            viz.drawPlacement(db, run_output_dir, info);

            // use python script to create gif from generated pngs in run directory
            std::string quiet_flag = quiet ? " --quiet" : "";
            std::string gif_command = "python3 tools/gif_builder.py " + run_output_dir + "/placement" + " -d 100 -o " + run_output_dir + "/full_placement.gif" + quiet_flag;
            system(gif_command.c_str());
        }
    #endif

    // Write placed design to DEF in run-specific directory
    db.writeDEF(run_output_dir);

    // Copy config file to run directory for reproducibility
    std::ifstream src(m_config_filepath);
    std::ofstream dst(run_output_dir + "/config_used.json");
    dst << src.rdbuf();

    Logger::log_info("All outputs saved to: " + run_output_dir);
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
    // add named focus nets
    auto& nets = db.getNets();
    if (cfg["output"].contains("focus_nets")) {
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

    std::mt19937 rng(std::random_device{}());

    // add random focus nets — sample from all nets
    int num_focus_nets = cfg["output"].value("rand_focus_nets", 0);
    if (num_focus_nets > 0) {
        std::vector<Net*> all_nets;
        all_nets.reserve(nets.size());
        for (auto& [id, net] : nets)
            all_nets.push_back(net);
        std::shuffle(all_nets.begin(), all_nets.end(), rng);
        int count = std::min(num_focus_nets, (int)all_nets.size());
        for (int i = 0; i < count; i++) {
            db.addFocusNet(all_nets[i]);
            Logger::log_info("Focus net (rand) " + std::to_string(i) + ": " + all_nets[i]->getName()
                + " (" + std::to_string(all_nets[i]->getNodes().size()) + " nodes)");
        }
    }

    // add random focus nodes — sample from movable components
    int num_focus_nodes = cfg["output"].value("rand_focus_nodes", 0);
    if (num_focus_nodes > 0) {
        std::vector<Node*> movable;
        for (auto& [name, comp] : db.getComponents())
            if (comp->getStatus() != FIXED)
                movable.push_back(comp);
        std::shuffle(movable.begin(), movable.end(), rng);
        int count = std::min(num_focus_nodes, (int)movable.size());
        for (int i = 0; i < count; i++) {
            db.addFocusNode(movable[i]);
            Logger::log_info("Focus node (rand) " + std::to_string(i) + ": " + movable[i]->getName()
                + " pos=(" + std::to_string(movable[i]->getPos().x) + ","
                + std::to_string(movable[i]->getPos().y) + ")");
        }
    }

    // add random macro nets — nets with at least one pin on a fixed macro (Component, not IOPad)
    int num_macro_nets = cfg["output"].value("rand_macro_nets", 0);
    if (num_macro_nets > 0) {
        std::vector<Net*> macro_nets;
        for (auto& [id, net] : nets) {
            for (Node* node : net->getNodes()) {
                if (dynamic_cast<Component*>(node) && node->getStatus() == FIXED) {
                    macro_nets.push_back(net);
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

    // add random focus IO — sample from IOPads and fixed components
    int num_focus_io = cfg["output"].value("rand_focus_IO", 0);
    if (num_focus_io > 0) {
        std::vector<Node*> fixed_nodes;
        for (auto& [name, pad] : db.getIOPads())
            fixed_nodes.push_back(pad);
        for (auto& [name, comp] : db.getComponents())
            if (comp->getStatus() == FIXED)
                fixed_nodes.push_back(comp);
        std::shuffle(fixed_nodes.begin(), fixed_nodes.end(), rng);
        int count = std::min(num_focus_io, (int)fixed_nodes.size());
        for (int i = 0; i < count; i++) {
            db.addFocusNode(fixed_nodes[i]);
            Logger::log_info("Focus IO (rand) " + std::to_string(i) + ": " + fixed_nodes[i]->getName()
                + " pos=(" + std::to_string(fixed_nodes[i]->getPos().x) + ","
                + std::to_string(fixed_nodes[i]->getPos().y) + ")");
        }
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
    if (overflow < best_fallback.overflow - OVFW_EPSILON ||
        (overflow < best_fallback.overflow + OVFW_EPSILON && hpwl < best_fallback.hpwl))
    {
        best_fallback = {hpwl, overflow, iteration, true};
        // Only snapshot if primary hasn't already saved this iteration
        if (!best_primary.valid || best_primary.iteration != iteration)
            snapshotBestPlacement();
    }
}

AIEPLACE_NAMESPACE_END
