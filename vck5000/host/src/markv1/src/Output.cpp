// Output.cpp
// Output, reporting, and results management functions
// Separated from AIEplace.cpp for better organization

#include "AIEplace.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

AIEPLACE_NAMESPACE_BEGIN

void Placer::printWelcomeBanner()
{
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

    Table info;
    info.add_row({"Version:", AIEPLACE_VERSION});
    info.format().hide_border();
    banner.add_row({info});
    banner.add_row({"VLSI global placement algorithm accelerated on AI Engines"});
    banner.add_row({}); // This line intentionally left blank

    banner.print(cout);
}

void Placer::printIterationResults()
{
    float hpwl = db.computeTotalWirelength(cfg["params"]["wirelength_method"]);
    hpwl_history.push_back(hpwl);

    //float learning_coeff = learning_rate * die_size;
    float learning_coeff = learning_rate * 1;
    learning_coeff_history.push_back(learning_coeff);

    float overflow = grid.computeTotalOverflow();
    //if(iteration >=10)
    ovfw_history.push_back(overflow);

    //Logger::log_data("HPWL = " + std::to_string(hpwl));
    // every 10 iterations, export a table in markdown
    if (iteration % 1 == 0)
    {
        Table top;
        top.add_row(RowStream{} << "Iteration" << iteration);
        top.add_row(RowStream{} << "HPWL" << SCI(hpwl));
        top.add_row(RowStream{} << "Overflow" << (overflow));
        top.add_row(RowStream{} << "Learning Rate" << PREC(learning_rate));
        top.add_row(RowStream{} << "Learning Coeff" << PREC(learning_coeff));
        top.add_row(RowStream{} << "Global Lambda" << PREC(global_lambda));
        top.column(0).format().font_align(FontAlign::right);
        top.column(1).format().font_align(FontAlign::left);
        Logger::log_data(top);
        cout << endl;
    }

    // every 10 iterations, export an image
    #ifdef CREATE_VISUALIZATION
        if(cfg["output"]["visualize"])
        if (iteration < 10 || iteration % int(cfg["output"]["iterations_per_export"]) == 0) {
            PlotInfo info = {iteration, learning_rate, hpwl_history.back(), global_lambda, overflow};
            viz.drawPlacement(db, output_dir / "placement", info);
            viz.drawElectricField(grid, output_dir / "efield", iteration);
        }
    #endif

    //Logger::export_eField(grid, output_dir, iteration);

    // Append the HPWL value to a file for later analysis
    std::ofstream hpwl_file;
    fs::path dir = output_dir;
    hpwl_file.open(dir.append("hpwl.dat"), std::ios_base::app);
    if(iteration == 0) {
        hpwl_file << "Iter, HPWL, OVFW, LR, LAMBDA" << endl; // Write header only for the first iteration
    }
    hpwl_file << std::setfill('0') << std::setw(3) << iteration << ", "
              << std::setprecision(2) << std::scientific << hpwl << ", "
              << std::setprecision(2) << std::scientific << overflow << ", "
              << std::setprecision(2) << std::scientific << learning_rate << ", "
              << std::setprecision(2) << std::scientific << global_lambda << endl;
    hpwl_file.close();
}

void Placer::plotHistories() {
#ifdef CREATE_VISUALIZATION
    fs::path data_dir = output_dir / "data";
    if (!fs::exists(data_dir))
        fs::create_directories(data_dir);

    // Create individual plots
    CairoPlotter hpwl_plotter(800, 600);
    hpwl_plotter.plotHistory(hpwl_history, "HPWL Convergence", "HPWL Value", 0.0, 0.5, 1.0);
    hpwl_plotter.savePNG(data_dir / "hpwl_history.png");

    CairoPlotter ovfw_plotter(800, 600);
    ovfw_plotter.plotHistory(ovfw_history, "Overflow", "OVFW Value", 0.0, 0.5, 1.0);
    ovfw_plotter.savePNG(data_dir / "ovfw_history.png");

    CairoPlotter coeff_plotter(800, 600);
    coeff_plotter.plotHistory(learning_coeff_history, "Learning Coefficient History", "Learning Coefficient", 1.0, 0.2, 0.2);
    coeff_plotter.savePNG(data_dir / "learning_coeff_history.png");

    // Create combined plot
    CairoPlotter dual_plotter(800, 600);
    dual_plotter.plotDualHistory(hpwl_history, ovfw_history,
                                "HPWL and Overflow History",
                                "HPWL (normalized)", "Overflow (normalized)");
    dual_plotter.savePNG(data_dir / "combined_history.png");
#endif
}

// Enhanced function to create organized output structure
void Placer::createRunOutputStructure(std::string& run_output_dir, std::string& run_id)
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

    // Generate run ID
    run_id = db.getBenchmarkName() + "_" + timestamp;

    // Create directory structure: ./results/<benchmark_name>/<timestamped_run_name>/
    fs::path results_base("results");
    fs::path benchmark_dir = results_base / db.getBenchmarkName();
    fs::path run_dir = benchmark_dir / (timestamp + "_" +
                                       cfg["params"]["partials_compute_method"].get<std::string>() + "_" +
                                       cfg["params"]["density_compute_method"].get<std::string>());

    if (!fs::exists(run_dir)) {
        fs::create_directories(run_dir);
    }

    run_output_dir = run_dir.string();

    Logger::log_info("Created run directory: " + run_output_dir);
}

// Enhanced printFinalResults with organized output structure
void Placer::printFinalResults()
{
    Logger::log_info("AIEplace algorithm complete.");

    // Use the output directory created in constructor
    std::string run_output_dir = output_dir.string();
    std::string run_id = generateRunId();

    // Calculate final metrics
    float final_hpwl = db.computeTotalWirelength(cfg["params"]["wirelength_method"]);
    float final_overflow = grid.computeTotalOverflow();
    float total_runtime = getInterval(pgrm_start_time, getTime());
    float iteration_avg = (iteration > 0) ? total_runtime / iteration : 0.0f;

    // Calculate HPWL improvement if initial HPWL was recorded
    float hpwl_improvement = 0.0f;
    bool has_improvement = false;
    if (initial_hpwl > 0) {
        hpwl_improvement = ((initial_hpwl - final_hpwl) / initial_hpwl) * 100.0f;
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
    if (has_improvement) {
        results.add_row(RowStream{} << "Initial HPWL" << std::scientific << std::setprecision(3) << initial_hpwl);
        results.add_row(RowStream{} << "HPWL improvement (%)" << std::fixed << std::setprecision(2) << hpwl_improvement);
    }
    results.add_row(RowStream{} << "Final Overflow" << std::scientific << std::setprecision(3) << final_overflow);
    results.column(0).format().font_align(FontAlign::right);
    results.column(1).format().font_align(FontAlign::left);

    Table hyperparams;
    hyperparams.add_row({"Hyperparameter", "Final Value"});
    hyperparams.add_row(RowStream{} << "gamma" << gamma);
    hyperparams.add_row(RowStream{} << "learning rate" << learning_rate);
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

    // Enhanced CSV export to global results.csv
    Logger::ProgramStatBlock stats;
    populateStatsBlock(stats, final_hpwl, final_overflow, total_runtime,
                      iteration_avg, hpwl_improvement, has_improvement, run_id);

    Logger::append_csv(stats, cfg["output"]["result_csv"]);

    // Generate visualization in run-specific directory
    #ifdef CREATE_VISUALIZATION
        if(cfg["output"]["visualize"]) {
            PlotInfo info = {iteration, learning_rate, final_hpwl, global_lambda, final_overflow};
            viz.drawPlacement(db, run_output_dir, info);
        }
    #endif

    // Write placed design to DEF in run-specific directory
    db.writeDEF(run_output_dir);

    // Copy config file to run directory for reproducibility
    std::string config_backup = run_output_dir + "/config_used.json";
    std::ifstream src("host/run_config.json");
    std::ofstream dst(config_backup);
    dst << src.rdbuf();

    Logger::log_info("All outputs saved to: " + run_output_dir);
}

// Helper function to populate the comprehensive stats block
void Placer::populateStatsBlock(Logger::ProgramStatBlock& stats,
                               float final_hpwl, float final_overflow,
                               float total_runtime, float iteration_avg,
                               float hpwl_improvement, bool has_improvement,
                               const std::string& run_id)
{
    // Generate timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream timestamp_ss;
    timestamp_ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");

    // Basic information
    stats.timestamp = timestamp_ss.str();
    stats.run_id = run_id;
    stats.design_name = db.getBenchmarkName();
    int net_count = db.getNetsVector().size();
    std::string category;
    if (net_count < 100000) {
        category = " (Small)";
    } else if (net_count < 500000) {
        category = " (Medium)";
    } else if (net_count < 1000000) {
        category = " (Large)";
    } else {
        category = " (XLarge)";
    }
    stats.benchmark_size = std::to_string(net_count) + category;

    // Configuration
    stats.partials_method = cfg["params"]["partials_compute_method"];
    stats.density_method = cfg["params"]["density_compute_method"];
    stats.output_dir = output_dir.string();
    stats.wirelength_method = cfg["params"]["wirelength_method"];
    stats.gamma = gamma;
    stats.init_learning_rate = cfg["params"]["init_learning_rate"];
    stats.max_iterations = cfg["params"]["max_iterations"];

    // Results
    stats.iteration_count = iteration;
    stats.final_hpwl = final_hpwl;
    stats.initial_hpwl = initial_hpwl;
    stats.hpwl_improvement = hpwl_improvement;
    stats.has_improvement = has_improvement;
    stats.final_overflow = final_overflow;
    stats.final_learning_rate = learning_rate;
    stats.convergence_reached = checkConvergence();

    // Timing
    stats.prgm_runtime = total_runtime;
    stats.db_IO_time = db_IO_time;
    stats.algo_time = algo_time;
    stats.iteration_avg_time = iteration_avg;

    // System metrics
    stats.memory_usage_mb = getMemoryUsageMB();

    // Status
    stats.success = true;  // If we got here, it succeeded
    stats.error_message = "";
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
    initial_hpwl = db.computeTotalWirelength(cfg["params"]["wirelength_method"]);
    Logger::log_info("Initial HPWL recorded: " + std::to_string(initial_hpwl));
}

void Placer::initializeFocus()
{
    // add named focus nets
    //for()

    // add random focus nets
    auto nets = db.getNets();
    auto iter = nets.begin();
    for(int i = 0; i < cfg["params"]["rand_focus_nets"]; i++) {
        // pick a random net to focus which has a pin
        //std::advance(iter, rand() % nets.size());
        while(!iter->second->hasPin())
            std::advance(iter, 1);
        db.addFocusNet(iter->second);
        std::advance(iter, 1);
    }
}

AIEPLACE_NAMESPACE_END
