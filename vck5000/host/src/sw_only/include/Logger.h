/**
 * @file Logger.h
 * @brief Static logger with an ordered severity scale, scientific-notation helpers, and
 *        tabulate-based table/report formatting.
 */
#pragma once

#include "Common.h"
#include "Grid.h"
#include <unordered_set>
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

/**
 * @brief Ordered severity scale. One console threshold admits every level at or above it, so
 *        raising the bar can never leave a chattier level enabled by accident — the failure
 *        mode of the old independently-toggled key set.
 *
 *        TRACE/DEBUG sit BELOW DETAIL on purpose: the run report captures DETAIL and above, so
 *        the two developer-dump levels stay opt-in and never bloat it. ITER (the per-iteration
 *        live-status line) sits just below INFO so a non-interactive run can keep every INFO
 *        message while dropping the live status, with the same single threshold.
 */
enum class LogLevel { TRACE = 0, DEBUG, DETAIL, ITER, INFO, WARNING, ERROR, CRITICAL, OFF };

class Logger {
private:
    // Define type to match tabulate's expected types:
    using MsgType = std::variant<std::string, const char*, std::string_view, tabulate::Table>;

    static LogLevel console_level;                  // console threshold; OFF disables the console
    static bool console_color;                      // ANSI on stdout — TTY only, never in a pipe
    static std::unordered_set<string> custom_keys;  // named channels, orthogonal to the scale

    // Full-detail run report: everything at REPORT_LEVEL and above goes here regardless of the
    // console threshold, so the console can stay curated without losing the nuisance detail.
    // Lines logged before the run directory exists are held in the backlog and flushed on open.
    static constexpr LogLevel REPORT_LEVEL = LogLevel::DETAIL;
    static std::ofstream report_file;
    static std::vector<string> report_backlog;

    struct FunctionStatBlock {
        long long total_time = 0;        // Total time in microseconds
        int call_count = 0;              // Number of calls
        long long min_time = LLONG_MAX;  // Minimum execution time
        long long max_time = 0;          // Maximum execution time
        std::vector<long long> recent_times; // Recent execution times for percentiles
    };

    static std::unordered_map<string, FunctionStatBlock> function_stats_map;

    static string render(const MsgType& msg);
    static void emit(const string& tag, const char* ansi, const string& text,
                     bool to_console, bool to_report);

public:
    // Setup functions
    static void setup_logging(LogLevel console);
    static void openReport(fs::path dir, string filename = "run.log");

    static inline void activate_logging_key(string key)
    { Logger::custom_keys.insert(key); }

    static inline void deactivate_logging_key(string key)
    { Logger::custom_keys.erase(key); }

    static inline bool isKeyActive(const string& key)
    { return Logger::custom_keys.count(key) > 0; }

    /// @brief True if @p level would reach the console or the report. Gate expensive diagnostic
    ///        computation on this so it is not built only to be discarded.
    static inline bool isLevelActive(LogLevel level)
    { return level >= console_level || level >= REPORT_LEVEL; }


    // Primary logging functions
    static bool log(LogLevel level, const MsgType& msg);
    static bool log_key(const string& key, const MsgType& msg);

    // inline functions for convenience
    static inline void log_trace(const MsgType& msg)    { log(LogLevel::TRACE, msg); }
    static inline void log_debug(const MsgType& msg)    { log(LogLevel::DEBUG, msg); }
    static inline void log_detail(const MsgType& msg)   { log(LogLevel::DETAIL, msg); }
    static inline void log_iter(const MsgType& msg)     { log(LogLevel::ITER, msg); }
    static inline void log_info(const MsgType& msg)     { log(LogLevel::INFO, msg); }
    static inline void log_warning(const MsgType& msg)  { log(LogLevel::WARNING, msg); }
    static inline void log_error(const MsgType& msg)    { log(LogLevel::ERROR, msg); }
    static inline void log_critical(const MsgType& msg) { log(LogLevel::CRITICAL, msg); }

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


