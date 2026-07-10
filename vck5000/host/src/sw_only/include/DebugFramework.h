#ifndef DEBUG_FRAMEWORK_H
#define DEBUG_FRAMEWORK_H

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <functional>
#include <stack>
#include <thread>
#include <atomic>
#include <ctime>
#include <algorithm>
#include <climits>

// Debug levels
enum class DebugLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL,
    NONE
};

// Forward declarations
class Timer;
class ScopeTimer;
class MemoryTracker;

// Main debugging framework class (Singleton)
class DebugFramework {
private:
    // Singleton pattern
    static DebugFramework* instance;
    static std::mutex instanceMutex;
    
    // Configuration
    DebugLevel currentLevel = DebugLevel::INFO;
    bool enableTiming = true;
    bool enableLogging = true;
    bool enableCallStack = true;
    bool enableMemoryTracking = false;
    std::string logFilename = "debug.log";
    std::ofstream logFile;
    
    // Profiling data
    using clock_type = std::chrono::high_resolution_clock;
    using time_point = clock_type::time_point;
    using duration = std::chrono::microseconds;
    
    struct FunctionStats {
        long long totalTime = 0;        // Total time in microseconds
        int callCount = 0;              // Number of calls
        long long minTime = LLONG_MAX;  // Minimum execution time
        long long maxTime = 0;          // Maximum execution time
        std::vector<long long> recentTimes; // Recent execution times for percentiles
    };
    
    struct MemoryStats {
        size_t currentUsage = 0;
        size_t peakUsage = 0;
        std::unordered_map<std::string, size_t> allocationsByFunction;
    };
    
    std::unordered_map<std::string, FunctionStats> functionStats;
    MemoryStats memoryStats;
    std::mutex statsMutex;
    
    // Call stack tracking
    std::stack<std::string> callStack;
    std::mutex callStackMutex;
    
    // Thread ID tracking
    std::unordered_map<std::thread::id, std::string> threadNames;
    std::mutex threadMutex;
    
    // Private constructor for singleton
    DebugFramework();
    
    // Prevent copying and assignment
    DebugFramework(const DebugFramework&) = delete;
    DebugFramework& operator=(const DebugFramework&) = delete;
    
public:
    // Singleton access
    static DebugFramework& getInstance();
    
    // Configuration methods
    void setDebugLevel(DebugLevel level);
    void setLogFile(const std::string& filename);
    void enableFeature(const std::string& feature, bool enable);
    void setThreadName(const std::string& name);
    
    // Logging
    void log(DebugLevel level, const std::string& message);
    
    // Function tracking
    void enterFunction(const std::string& functionName);
    void exitFunction(const std::string& functionName, long long durationMicroseconds);
    
    // Memory tracking
    void trackAllocation(const std::string& functionName, size_t bytes);
    void trackDeallocation(const std::string& functionName, size_t bytes);
    
    // Reporting
    std::string generateReport();
    void saveReportToFile(const std::string& filename);
    void resetStatistics();
    
    // Destructor
    ~DebugFramework();
};

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
    DebugLevel mLogLevel;

public:
    ScopeTimer(const std::string& name, DebugLevel logLevel = DebugLevel::DEBUG);
    ~ScopeTimer();
    
    // Non-copyable
    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;
};

// RAII-style memory tracker
class MemoryTracker {
private:
    std::string mFunctionName;
    size_t mBytes;

public:
    MemoryTracker(const std::string& functionName, size_t bytes);
    ~MemoryTracker();
    
    // Non-copyable
    MemoryTracker(const MemoryTracker&) = delete;
    MemoryTracker& operator=(const MemoryTracker&) = delete;
};

// Custom memory allocator template
template<typename T>
class DebugAllocator {
public:
    using value_type = T;
    
    DebugAllocator() = default;
    
    template<typename U>
    DebugAllocator(const DebugAllocator<U>&) {}
    
    T* allocate(size_t n) {
        T* ptr = new T[n];
        DebugFramework::getInstance().trackAllocation(__func__, n * sizeof(T));
        return ptr;
    }
    
    void deallocate(T* p, size_t n) {
        DebugFramework::getInstance().trackDeallocation(__func__, n * sizeof(T));
        delete[] p;
    }
};

// Convenient macros for easy usage
#define LOG_TRACE(msg) DebugFramework::getInstance().log(DebugLevel::TRACE, msg)
#define LOG_DEBUG(msg) DebugFramework::getInstance().log(DebugLevel::DEBUG, msg)
#define LOG_INFO(msg) DebugFramework::getInstance().log(DebugLevel::INFO, msg)
#define LOG_WARN(msg) DebugFramework::getInstance().log(DebugLevel::WARN, msg)
#define LOG_ERROR(msg) DebugFramework::getInstance().log(DebugLevel::ERROR, msg)
#define LOG_CRITICAL(msg) DebugFramework::getInstance().log(DebugLevel::CRITICAL, msg)

#define TIME_FUNCTION() ScopeTimer scopeTimer(__func__)
#define TIME_BLOCK(name) ScopeTimer scopeTimer(name)
#define TRACK_MEMORY(bytes) MemoryTracker memTracker(__func__, bytes)

#endif // DEBUG_FRAMEWORK_H