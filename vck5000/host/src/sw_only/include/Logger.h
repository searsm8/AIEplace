/**
 * @file Logger.h
 * @brief Singleton logger with severity keys, scientific-notation helpers, and tabulate-based
 *        table/report formatting.
 */
#pragma once

#include "Common.h"
#include "Grid.h"
#include <unordered_set>
#include <mutex>
#include <climits>
#include <variant>
#include <tabulate/table.hpp>
#include <tabulate/markdown_exporter.hpp>
#include <sstream>
#include <iomanip>

// tabulate types are referenced as tabulate:: in this header; .cpp files that build tables
// declare a file-local `using namespace tabulate;` (no namespace leak through the header).
using std::string;

// Macros for convenient logging in scientific notation
inline std::string SCI(double val) {
    std::ostringstream oss;
    oss << std::scientific << std::setprecision(3) << val;
    return oss.str();
}
inline std::string SCI_P(double val, int prec) {
    std::ostringstream oss;
    oss << std::scientific << std::setprecision(prec) << val;
    return oss.str();
}
inline std::string PREC(double val) {
    std::ostringstream oss;
    oss << std::setprecision(3) << val;
    return oss.str();
}
inline std::string PREC_P(double val, int prec) {
    std::ostringstream oss;
    oss << std::setprecision(prec) << val;
    return oss.str();
}

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
    using MsgType = std::variant<std::string, const char*, std::string_view, tabulate::Table>;

    static std::unordered_set<string> keys;
    static std::map<string, tabulate::Color> string_colors;


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
    // Singleton access
    static Logger& getLogger();
    static Logger& getMutex();

    // Setup functions
    static void setup_logging(bool quiet = false);

    static inline void activate_logging_key(string key)
    { Logger::keys.insert(key); }

    static inline void deactivate_logging_key(string key)
    { Logger::keys.erase(key); }

    static inline bool isKeyActive(const string& key)
    { return Logger::keys.count(key) > 0; }


    // Primary logging fucntions
    static bool log(string key, MsgType msg);

    static tabulate::Color getColor(string key);

    // inline functions for convenience
    static inline void log_trace(const MsgType& msg)    { iLogger->log("TRACE", msg); }
    static inline void log_detail(const MsgType& msg)   { iLogger->log("DETAIL", msg); }
    static inline void log_debug(const MsgType& msg)    { iLogger->log("DEBUG", msg); }
    static inline void log_iter(const MsgType& msg)      { iLogger->log("ITER", msg); }
    static inline void log_info(const MsgType& msg)     { iLogger->log("INFO", msg); }
    static inline void log_warning(const MsgType& msg)  { iLogger->log("WARNING", msg); }
    static inline void log_error(const MsgType& msg)    { iLogger->log("ERROR", msg); }
    static inline void log_critical(const MsgType& msg) { iLogger->log("CRITICAL", msg); }

    // Report generation functions
    static void export_markdown(tabulate::Table t, fs::path dir, string filename = "statistics");
    static void export_eField(AIEplace::Grid& grid, fs::path dir, int iter);
    static void updateFunctionStats(string func_name, long long func_time);
    static tabulate::Table printFunctionStats();
    static double getFunctionTime(const std::string& func_name);

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


