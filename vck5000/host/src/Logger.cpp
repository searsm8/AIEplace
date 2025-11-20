// Logger.cpp

#include "Logger.h"

// Initialize static members of Logger
std::mutex Logger::iMutex;
Logger* Logger::iLogger = nullptr;
std::unordered_set<string> Logger::keys;
std::map<string, Color> Logger::string_colors;
std::unordered_map<string, Logger::FunctionStatBlock> Logger::function_stats_map;

// Logger implementation
Logger::Logger() { }

Logger& Logger::getLogger() {
    std::lock_guard<std::mutex> lock(iMutex);
    if (iLogger == nullptr) {
        iLogger = new Logger();
    }
    return *iLogger;
}
void Logger::setup_logging()
{
    // DEFAULT LOGGING KEYS
    //Logger::activate_logging_key("TRACE");  // For program execution tracing, e.g. function calls
    Logger::activate_logging_key("DETAIL"); // Used to give detail on things that most people won't care about. Usually off.
    //Logger::activate_logging_key("DEBUG");  // Used only by developers for debugging!
    Logger::activate_logging_key("DATA");   // Used to give info on things that some people might care about. Usually on.
    Logger::activate_logging_key("INFO");   // Used to give data on things that most people will care about. Always on.
    Logger::activate_logging_key("WARNING");// Something bad has happened!
    Logger::activate_logging_key("ERROR");  // Something VERY bad has happened!
    Logger::activate_logging_key("CRITICAL");// Something devastating has happened, must exit program!

    // CUSTOM LOGGING KEYS
    //Logger::activate_logging_key("packets"); // Used in DataBase.cpp for packet initialization
    Logger::activate_logging_key("dbinfo");
    Logger::activate_logging_key("profiling");
    Logger::string_colors["profiling"] = Color::yellow;
    //Logger::activate_logging_key("bins");
    //Logger::activate_logging_key("overlap");
    //Logger::activate_logging_key("function"); // Used to log when important functions are called

    // Logging colors
    Logger::string_colors["TRACE"] = Color::grey;
    Logger::string_colors["DETAIL"] = Color::cyan;
    Logger::string_colors["DEBUG"] = Color::magenta;
    Logger::string_colors["INFO"] = Color::green;
    Logger::string_colors["DATA"] = Color::blue;
    Logger::string_colors["WARNING"] = Color::yellow;
    Logger::string_colors["ERROR"] = Color::red;
    Logger::string_colors["CRITICAL"] = Color::red;
}

bool Logger::log(string key, MsgType msg)
{
    // if the key is in the active key set, print it
    if(Logger::keys.find(key) == Logger::keys.end())
        return false;
    else {
        Table top;
        //top.add_row({"key", "msg"});
        top.add_row({key, msg});
        top.format().hide_border().font_align(FontAlign::center);
        top.column(0).format()
            .width(12)
            .font_style({FontStyle::bold, FontStyle::italic})
            .font_align(FontAlign::right)
            .font_color(getColor(key));
        top.print(std::cout);
        return true;
    }
}

//bool Logger::log(KeyType key, std::string msg)
//{
//    // if the key is in the active key set, print it
//    if(Logger::keys.find(key) == Logger::keys.end())
//        return false;
//    else {
//        Table table;
//        table.add_row({"["+key+"]", msg});
//
//        table.format().hide_border();
//
//        table.column(0).format()
//            .width(12)
//            .font_style({FontStyle::bold, FontStyle::italic})
//            .font_align(FontAlign::right)
//            .font_color(getColor(key));
//
//        table.column(1).format()
//            .border_left(":");
//
//        table.print(std::cout);
//        return true;
//    }
//}

//bool Logger::log(string key, string msg)
//{
//    return true;
//}

//bool log_detail(std::string msg)
//{ return log("DETAIL", msg); }
//bool log_info(std::string msg)
//{ return log("INFO", msg); }
//bool log_debug(std::string msg)
//{ return log("DEBUG", msg); }
//bool log_warning(std::string msg)
//{ return log("WARNING", msg); }
//bool log_error(std::string msg)
//{ return log("ERROR", msg); }
//bool log_data(std::string msg)
//{ return log("DATA", msg); }
//
//bool log_detail(Table t)
//{ return log("DETAIL", t); }
//bool log_info(Table t)
//{ return log("INFO", t); }
//bool log_debug(Table t)
//{ return log("DEBUG", t); }
//bool log_warning(Table t)
//{ return log("WARNING", t); }
//bool log_error(Table t)
//{ return log("ERROR", t); }
//bool log_data(Table t)
//{ return log("DATA", t); }
//
//void log_space()
//{
//    std::cout << std::endl;
//}

Color Logger::getColor(string key)
{
        if ( string_colors.find(key) == string_colors.end() )
            return Color::white;
        else return string_colors[key];

}

void Logger::export_markdown(Table t, fs::path dir, string filename)
{
    // Use exporter
    MarkdownExporter exporter;
    auto markdown = exporter.dump(t);

    // Write to file
    std::ofstream out_file;
    out_file.open(dir.append(filename + ".md"));
    out_file << markdown;
    out_file.close();
}

// used for debugging, enables easy comparison of CPU and AIE results
// export intermdiate results of computations such as density or partials terms
void Logger::export_eField(AIEplace::Grid& grid, fs::path dir, int iter)
{
    std::ofstream eField_file;
    eField_file.open(dir.append("intermed_eField.dat"), std::ios_base::app);
    eField_file << endl << "Iteration: " << iter << endl;
    eField_file << "eField.x" << endl;
    for (int x = 0; x < grid.getBinsPerRow(); x++) {
        eField_file << "row " << x << ": ";
        for (int y = 0; y < grid.getBinsPerCol(); y++) {
            eField_file << grid.getBin(x, y).eField.x << " ";
        }
        eField_file << endl;
    }

    eField_file << endl << "eField.y" << endl;
    for (int x = 0; x < grid.getBinsPerRow(); x++) {
        eField_file << "row " << x << ": ";
        for (int y = 0; y < grid.getBinsPerCol(); y++) {
            eField_file << grid.getBin(x, y).eField.y << " ";
        }
        eField_file << endl;
    }

    eField_file.close();

}

// Enhanced Logger::append_csv function with comprehensive metrics
void Logger::append_csv(ProgramStatBlock &stats, string filename)
{
    // Create results directory if it doesn't exist
    fs::path results_dir("results");
    if (!fs::exists(results_dir)) {
        fs::create_directories(results_dir);
    }
    
    fs::path csv_path = results_dir / filename;
    
    // Open file for appending
    std::ofstream out_file;
    bool need_header = !fs::exists(csv_path);
    out_file.open(csv_path, std::ios_base::app);
    out_file.imbue(std::locale::classic());  // Prevent comma thousands separators
    
    // If file doesn't exist, create and write comprehensive header
    if(need_header) {
        // Basic run information
        out_file << "Design,";
        
        // Configuration
        out_file << "Partials_Method,";
        out_file << "Density_Method,";
        out_file << "Gamma,";
        out_file << "Init_Learning_Rate,";
        
        // Results
        out_file << "Iterations,";
        out_file << "Final_HPWL,";
        out_file << "Initial_HPWL,";
        out_file << "HPWL_Improvement_Percent,";
        out_file << "Final_Overflow,";
        out_file << "Final_Learning_Rate,";
        
        // Timing (high-level)
        out_file << "Benchmark_Size,";
        out_file << "Total_Runtime_Sec,";
        out_file << "DB_IO_Time_Sec,";
        out_file << "Algorithm_Time_Sec,";
        out_file << "Iteration_Avg_Sec,";
        
        // Detailed function timing
        out_file << "Partials_AIE_Time,";
        //out_file << "Density_Compute_Time,";
        //out_file << "Placement_Update_Time,";
        
        // System metrics
        out_file << "Memory_Usage_MB,";
        out_file << "AIE_Utilization_Percent,";
        
        // Status
        out_file << "Success,";
        out_file << "Error_Message,";
        out_file << "Timestamp";
        
        out_file << endl;
    }
    
    // Write data row
    out_file << "\"" << stats.design_name << "\",";
    
    // Configuration
    out_file << "\"" << stats.partials_method << "\",";
    out_file << "\"" << stats.density_method << "\",";
    out_file << stats.gamma << ",";
    out_file << stats.init_learning_rate << ",";
    
    // Results
    out_file << stats.iteration_count << ",";
    out_file << std::scientific << stats.final_hpwl << ",";
    out_file << std::scientific << stats.initial_hpwl << ",";
    out_file << std::fixed << std::setprecision(2) << stats.hpwl_improvement << ",";
    out_file << std::scientific << stats.final_overflow << ",";
    out_file << std::fixed << std::setprecision(6) << stats.final_learning_rate << ",";
    
    // Timing (high-level)
    out_file << "\"" << stats.benchmark_size << "\",";
    out_file << std::fixed << std::setprecision(3);
    out_file << stats.prgm_runtime << ",";
    out_file << stats.db_IO_time << ",";
    out_file << stats.algo_time << ",";
    out_file << stats.iteration_avg_time << ",";
    
    // Detailed function timing (with safety checks)
    auto safe_get_time = [](const std::string& func_name) -> double {
        if (function_stats_map.find(func_name) != function_stats_map.end()) {
            return function_stats_map[func_name].total_time;
        }
        return 0.0;
    };
    
    out_file << safe_get_time("computeAllPartials_AIE") << ",";
    //out_file << safe_get_time("computeDensity") << ",";
    //out_file << safe_get_time("updatePlacements") << ",";
    
    // System metrics
    out_file << stats.memory_usage_mb << ",";
    out_file << stats.aie_utilization_percent << ",";
    
    // Status
    out_file << (stats.success ? "TRUE" : "FALSE") << ",";
    out_file << "\"" << stats.error_message << "\",";
    out_file << "\"" << stats.timestamp << "\"";
    
    out_file << endl;
    out_file.close();
}


void Logger::updateFunctionStats(string func_name, long long func_time)
{
    std::lock_guard<std::mutex> lock(iMutex); // thread-safe access
    // Check if the function name already exists in the map
    FunctionStatBlock & s = function_stats_map[func_name];
    s.call_count++;
    s.total_time += func_time;

    if(func_time < s.min_time)
        s.min_time = func_time;
    if(func_time > s.max_time)
        s.max_time = func_time;
    s.recent_times.push_back(func_time);
}

Table Logger::printFunctionStats()
{
    Table top;
    top.add_row(RowStream{} << "Function" << "calls" << "total time (us)" << "min" << "max" << "avg");
    for(auto item : function_stats_map) {
        FunctionStatBlock &s = item.second;
        long avg = std::accumulate(s.recent_times.begin(), s.recent_times.end(), 0) / s.recent_times.size();
        top.add_row(RowStream{} << item.first << s.call_count << s.total_time << s.min_time << s.max_time << avg);
    }

    log("profiling", top);
    return top;
}


/*************************************************
 * Timer Implementation
 */
Timer::Timer(const std::string& name, bool autoStart) 
    : mName(name) {
    if (autoStart) {
        start();
    }
}

void Timer::start() {
    if (!mIsRunning) {
        mTimeStart = clock_type::now();
        mIsRunning = true;
    }
}

long long Timer::stop() {
    if (!mIsRunning) {
        return 0;
    }
    
    time_point timeEnd = clock_type::now();
    long long elapsedMicroseconds = std::chrono::duration_cast<duration>(timeEnd - mTimeStart).count();
    mTotalMicroseconds += elapsedMicroseconds;
    mIsRunning = false;
    
    return elapsedMicroseconds;
}

void Timer::reset(bool autoStart) {
    mTotalMicroseconds = 0;
    mIsRunning = false;
    if (autoStart) {
        start();
    }
}

long long Timer::elapsed() const {
    if (!mIsRunning) {
        return mTotalMicroseconds;
    }
    
    time_point timeNow = clock_type::now();
    return mTotalMicroseconds + 
           std::chrono::duration_cast<duration>(timeNow - mTimeStart).count();
}

long long Timer::total() const {
    return mTotalMicroseconds;
}

void Timer::setName(const std::string& name) {
    mName = name;
}

const std::string& Timer::getName() const {
    return mName;
}

// ScopeTimer implementation
ScopeTimer::ScopeTimer(const std::string& name, string log_key) 
    : mName(name), mLogKey(log_key) {
    mTimer.start();
    // Track function entry
    //DebugFramework::getInstance().enterFunction(mName);
}

ScopeTimer::~ScopeTimer() {
    long long elapsed = mTimer.stop();
    Logger::updateFunctionStats(mName, elapsed);
    //Logger::log("profiling", mName + " took " + std::to_string(elapsed) + " microseconds");
}