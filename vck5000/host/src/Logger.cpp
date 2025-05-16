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
void Logger::export_intermediate_results(AIEplace::Grid& grid, fs::path dir, int iter)
{
    std::ofstream out_file;
    out_file.open(dir.append("intermed_eField.dat"), std::ios_base::app);
    out_file << endl << "Iteration: " << iter << endl;
    out_file << "eField.x" << endl;
    for (int x = 0; x < grid.getBinsPerRow(); x++) {
        out_file << "row " << x << ": ";
        for (int y = 0; y < grid.getBinsPerCol(); y++) {
            out_file << grid.getBin(x, y).eField.x << " ";
        }
        out_file << endl;
    }

    out_file << endl << "eField.y" << endl;
    for (int x = 0; x < grid.getBinsPerRow(); x++) {
        out_file << "row " << x << ": ";
        for (int y = 0; y < grid.getBinsPerCol(); y++) {
            out_file << grid.getBin(x, y).eField.y << " ";
        }
        out_file << endl;
    }

    out_file.close();
}

void Logger::append_csv(ProgramStatBlock &stats)
{
    std::ofstream out_file;
    fs::path p("results/run_statistics.csv");
    bool need_header = !fs::exists(p);
    // Append to CSV file
    out_file.open("results/run_statistics.csv", std::ios_base::app);

    // If file DNE, create and write header
    if(need_header) {
        out_file << "Design, ";
        out_file << "Iterations, ";
        out_file << "PARTIALS_GRAPH_COUNT, ";
        out_file << "HPWL, ";
        out_file << "Learning Rate, ";
        //out_file << "Runtime, ";
        //out_file << "IO time, ";
        out_file << "Algo time, ";
        out_file << "Partials CPU time, ";
        out_file << "Partials AIE time, ";
        out_file << "computePartials AIE, ";
        out_file << "receivePartials AIE, ";
        out_file << "lock time, ";
        out_file << endl;
    }

    // Add new entry for this design
    out_file << stats.design_name << ", ";
    out_file << stats.iteration_count << ", ";
    out_file << PARTIALS_GRAPH_COUNT << ", ";
    out_file << std::scientific;
    out_file << std::to_string(stats.final_hpwl) << ", ";
    out_file << std::fixed << std::setprecision(3);
    out_file << stats.final_learning_rate << ", ";
    //out_file << std::to_string(stats.prgm_runtime) << ", ";
    //out_file << std::to_string(stats.db_IO_time) << ", ";
    out_file << std::to_string(function_stats_map["Algorithm Block"].total_time) << ", ";
    out_file << std::to_string(function_stats_map["computeAllPartials_CPU"].total_time) << ", ";
    out_file << std::to_string(function_stats_map["computeAllPartials_AIE"].total_time) << ", ";
    out_file << std::to_string(function_stats_map["computePartials"].total_time) << ", ";
    out_file << std::to_string(function_stats_map["receivePartials"].total_time) << ", ";
    out_file << std::to_string(function_stats_map["lockNodes"].total_time) << ", ";
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