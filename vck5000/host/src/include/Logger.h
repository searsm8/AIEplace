// Logger.h
// Simple logger for debugging and managing info and data tables.
#ifndef LOGGER_H
#define LOGGER_H

#include "Common.h"
#include "Grid.h"
#include <unordered_set>
#include <mutex>
#include <climits>
#include <variant>
#include <tabulate/table.hpp>
#include <tabulate/markdown_exporter.hpp>

using namespace tabulate;
using std::string;

// Macros for convenient logging in scientific notation
#define SCI(val) \
    (static_cast<std::ostringstream&>(std::ostringstream() << std::scientific << std::setprecision(2) << val)).str()

#define SCI_P(val, prec) \
    (static_cast<std::ostringstream&>(std::ostringstream() << std::scientific << std::setprecision(prec) << val)).str()

#define PREC(val) \
    (static_cast<std::ostringstream&>(std::ostringstream() << std::setprecision(2) << val)).str()

#define PREC_P(val, prec) \
    (static_cast<std::ostringstream&>(std::ostringstream() << std::setprecision(prec) << val)).str()

// Forward declarations
class Timer;
class ScopeTimer;
class MemoryTracker;



class Logger {
private:
    // Singleton class pattern
    static Logger* iLogger; // single instance of logger
    static std::mutex iMutex;

    // Define type to match tabulate's expected types:
    using MsgType = std::variant<std::string, const char*, std::string_view, Table>;
    //using MsgType = std::variant<string, Table>;

    static std::unordered_set<string> keys;
    static std::map<string, Color> string_colors;


    struct FunctionStatBlock {
        long long total_time = 0;        // Total time in microseconds
        int call_count = 0;              // Number of calls
        long long min_time = LLONG_MAX;  // Minimum execution time
        long long max_time = 0;          // Maximum execution time
        std::vector<long long> recent_times; // Recent execution times for percentiles
    };

    static std::unordered_map<string, FunctionStatBlock> function_stats_map;

    // Constructor, private for singleton
    Logger();

public:
    struct ProgramStatBlock {
        // Basic information
        std::string timestamp;
        std::string run_id;
        std::string design_name;
        std::string benchmark_size;
        std::string output_dir;
        
        // Configuration
        std::string partials_method;
        std::string density_method;
        std::string wirelength_method;
        float gamma;
        float init_alpha;
        int max_iterations;
        
        // Results
        int iteration_count;
        float final_hpwl;
        float initial_hpwl;
        float hpwl_improvement;
        bool has_improvement;
        float final_overflow;
        float final_alpha;
        bool convergence_reached;
        
        // Timing
        float prgm_runtime;
        float db_IO_time;
        float algo_time;
        float AIE_time;
        float iteration_avg_time;
        
        // System metrics
        float memory_usage_mb;
        
        // Status
        bool success;
        std::string error_message;
    };

    // Singleton access
    static Logger& getLogger();
    static Logger& getMutex();

    // Setup functions
    static void setup_logging();

    static inline void activate_logging_key(string key)
    { Logger::keys.insert(key); }

    static inline void deactivate_logging_key(string key)
    { Logger::keys.erase(key); }


    // Primary logging fucntions
    static bool log(string key, MsgType msg);

    static Color getColor(string key);

    // inline functions for convenience
    static inline void log_trace(const MsgType& msg)    { iLogger->log("TRACE", msg); }
    static inline void log_detail(const MsgType& msg)   { iLogger->log("DETAIL", msg); }
    static inline void log_debug(const MsgType& msg)    { iLogger->log("DEBUG", msg); }
    static inline void log_data(const MsgType& msg)     { iLogger->log("DATA", msg); }
    static inline void log_info(const MsgType& msg)     { iLogger->log("INFO", msg); }
    static inline void log_warning(const MsgType& msg)  { iLogger->log("WARNING", msg); }
    static inline void log_error(const MsgType& msg)    { iLogger->log("ERROR", msg); }
    static inline void log_critical(const MsgType& msg) { iLogger->log("CRITICAL", msg); }

    // Report generation functions
    static void export_markdown(Table t, fs::path dir, string filename = "statistics");
    static void append_csv(ProgramStatBlock &, string filename = "run_statistics.csv");
    static void export_eField(AIEplace::Grid& grid, fs::path dir, int iter);
    static void updateFunctionStats(string func_name, long long func_time);
    static Table printFunctionStats();

}; // end class Logger

// Timer class for measuring elapsed time
class Timer {
private:
    using clock_type = std::chrono::high_resolution_clock;
    using time_point = clock_type::time_point;
    using duration = std::chrono::microseconds;
    
    time_point mTimeStart;
    long long mTotalMicroseconds = 0;
    bool mIsRunning = false;
    std::string mName;

public:
    // Constructor with optional name and auto-start
    Timer(const std::string& name = "", bool autoStart = true);
    
    // Core timer operations
    void start();
    long long stop();
    void reset(bool autoStart = true);
    
    // Timer information
    long long elapsed() const;
    long long total() const;
    void setName(const std::string& name);
    const std::string& getName() const;
};

// RAII-style timer for automatic function/scope tracking
class ScopeTimer {
private:
    Timer mTimer;
    std::string mName;
    string mLogKey;

public:
    ScopeTimer(const std::string& name, string log_key = "profiling");
    ~ScopeTimer();
    
    // Non-copyable
    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;
};

#define TIME_FUNCTION() ScopeTimer scopeTimer(__func__)
#define TIME_BLOCK(name) ScopeTimer scopeTimer(name)


#endif