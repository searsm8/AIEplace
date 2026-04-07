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
#include <atomic>
#include <ctime>

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
    DebugFramework() {
        if (enableLogging) {
            logFile.open(logFilename, std::ios::out | std::ios::app);
        }
    }
    
public:
    // Singleton access
    static DebugFramework& getInstance() {
        std::lock_guard<std::mutex> lock(instanceMutex);
        if (instance == nullptr) {
            instance = new DebugFramework();
        }
        return *instance;
    }
    
    // Configuration methods
    void setDebugLevel(DebugLevel level) {
        currentLevel = level;
    }
    
    void setLogFile(const std::string& filename) {
        if (logFile.is_open()) {
            logFile.close();
        }
        logFilename = filename;
        logFile.open(logFilename, std::ios::out | std::ios::app);
    }
    
    void enableFeature(const std::string& feature, bool enable) {
        if (feature == "timing") enableTiming = enable;
        else if (feature == "logging") enableLogging = enable;
        else if (feature == "callstack") enableCallStack = enable;
        else if (feature == "memory") enableMemoryTracking = enable;
    }
    
    // Name the current thread
    void setThreadName(const std::string& name) {
        std::lock_guard<std::mutex> lock(threadMutex);
        threadNames[std::this_thread::get_id()] = name;
    }
    
    // Log message with level
    void log(DebugLevel level, const std::string& message) {
        if (!enableLogging || level < currentLevel) return;
        
        // Get current time
        auto now = std::chrono::system_clock::now();
        auto nowTime = std::chrono::system_clock::to_time_t(now);
        
        // Format timestamp
        std::stringstream ss;
        ss << std::put_time(std::localtime(&nowTime), "%Y-%m-%d %H:%M:%S");
        
        // Get thread name or ID
        std::string threadId;
        {
            std::lock_guard<std::mutex> lock(threadMutex);
            auto it = threadNames.find(std::this_thread::get_id());
            if (it != threadNames.end()) {
                threadId = it->second;
            } else {
                std::stringstream tid;
                tid << std::this_thread::get_id();
                threadId = tid.str();
            }
        }
        
        // Level to string
        std::string levelStr;
        switch (level) {
            case DebugLevel::TRACE: levelStr = "TRACE"; break;
            case DebugLevel::DEBUG: levelStr = "DEBUG"; break;
            case DebugLevel::INFO: levelStr = "INFO"; break;
            case DebugLevel::WARN: levelStr = "WARN"; break;
            case DebugLevel::ERROR: levelStr = "ERROR"; break;
            case DebugLevel::CRITICAL: levelStr = "CRITICAL"; break;
            default: levelStr = "UNKNOWN";
        }
        
        // Format message
        std::string formattedMsg = ss.str() + " [" + threadId + "] [" + levelStr + "] " + message;
        
        // Output to console and file
        std::cout << formattedMsg << std::endl;
        if (logFile.is_open()) {
            logFile << formattedMsg << std::endl;
            logFile.flush();
        }
    }
    
    // Function entry
    void enterFunction(const std::string& functionName) {
        if (enableCallStack) {
            std::lock_guard<std::mutex> lock(callStackMutex);
            callStack.push(functionName);
            
            // Log entry at TRACE level
            if (enableLogging && currentLevel <= DebugLevel::TRACE) {
                std::string indent(callStack.size() - 1, ' ');
                log(DebugLevel::TRACE, indent + "-> " + functionName);
            }
        }
    }
    
    // Function exit
    void exitFunction(const std::string& functionName, long long durationMicroseconds) {
        // Update function stats
        if (enableTiming) {
            std::lock_guard<std::mutex> lock(statsMutex);
            FunctionStats& stats = functionStats[functionName];
            stats.totalTime += durationMicroseconds;
            stats.callCount++;
            stats.minTime = std::min(stats.minTime, durationMicroseconds);
            stats.maxTime = std::max(stats.maxTime, durationMicroseconds);
            
            // Keep last 100 times for percentile calculations
            stats.recentTimes.push_back(durationMicroseconds);
            if (stats.recentTimes.size() > 100) {
                stats.recentTimes.erase(stats.recentTimes.begin());
            }
        }
        
        if (enableCallStack) {
            std::lock_guard<std::mutex> lock(callStackMutex);
            if (!callStack.empty()) {
                // Log exit at TRACE level
                if (enableLogging && currentLevel <= DebugLevel::TRACE) {
                    std::string indent(callStack.size() - 1, ' ');
                    log(DebugLevel::TRACE, indent + "<- " + functionName + " (" + 
                        std::to_string(durationMicroseconds) + "μs)");
                }
                callStack.pop();
            }
        }
    }
    
    // Memory tracking
    void trackAllocation(const std::string& functionName, size_t bytes) {
        if (!enableMemoryTracking) return;
        
        std::lock_guard<std::mutex> lock(statsMutex);
        memoryStats.currentUsage += bytes;
        memoryStats.peakUsage = std::max(memoryStats.peakUsage, memoryStats.currentUsage);
        memoryStats.allocationsByFunction[functionName] += bytes;
    }
    
    void trackDeallocation(const std::string& functionName, size_t bytes) {
        if (!enableMemoryTracking) return;
        
        std::lock_guard<std::mutex> lock(statsMutex);
        memoryStats.currentUsage = (memoryStats.currentUsage >= bytes) ? 
                                   memoryStats.currentUsage - bytes : 0;
    }
    
    // Generate profiling report
    std::string generateReport() {
        std::lock_guard<std::mutex> lock(statsMutex);
        
        std::stringstream report;
        report << "===== Debug Framework Report =====\n";
        report << "Generated: " << std::put_time(std::localtime(&std::time(nullptr)), "%Y-%m-%d %H:%M:%S") << "\n\n";
        
        // Function timing statistics
        if (enableTiming && !functionStats.empty()) {
            report << "== Function Timing Statistics ==\n";
            report << std::setw(30) << std::left << "Function" 
                   << std::setw(10) << std::right << "Calls" 
                   << std::setw(15) << "Total(μs)" 
                   << std::setw(15) << "Avg(μs)" 
                   << std::setw(15) << "Min(μs)" 
                   << std::setw(15) << "Max(μs)"
                   << std::setw(15) << "P90(μs)"
                   << "\n";
            
            report << std::string(115, '-') << "\n";
            
            for (const auto& entry : functionStats) {
                const auto& stats = entry.second;
                double avgTime = stats.callCount > 0 ? 
                                static_cast<double>(stats.totalTime) / stats.callCount : 0;
                
                // Calculate 90th percentile
                long long p90 = 0;
                if (!stats.recentTimes.empty()) {
                    auto sortedTimes = stats.recentTimes;
                    std::sort(sortedTimes.begin(), sortedTimes.end());
                    size_t p90Index = static_cast<size_t>(sortedTimes.size() * 0.9);
                    p90 = sortedTimes[p90Index];
                }
                
                report << std::setw(30) << std::left << entry.first 
                       << std::setw(10) << std::right << stats.callCount 
                       << std::setw(15) << stats.totalTime 
                       << std::setw(15) << std::fixed << std::setprecision(2) << avgTime
                       << std::setw(15) << stats.minTime 
                       << std::setw(15) << stats.maxTime
                       << std::setw(15) << p90
                       << "\n";
            }
            report << "\n";
        }
        
        // Memory statistics
        if (enableMemoryTracking) {
            report << "== Memory Statistics ==\n";
            report << "Current Memory Usage: " << memoryStats.currentUsage << " bytes\n";
            report << "Peak Memory Usage: " << memoryStats.peakUsage << " bytes\n\n";
            
            if (!memoryStats.allocationsByFunction.empty()) {
                report << "Memory Allocations by Function:\n";
                for (const auto& entry : memoryStats.allocationsByFunction) {
                    report << "  " << entry.first << ": " << entry.second << " bytes\n";
                }
                report << "\n";
            }
        }
        
        return report.str();
    }
    
    // Save report to file
    void saveReportToFile(const std::string& filename) {
        std::ofstream reportFile(filename);
        if (reportFile.is_open()) {
            reportFile << generateReport();
            reportFile.close();
        }
    }
    
    // Clear all statistics
    void resetStatistics() {
        std::lock_guard<std::mutex> lock(statsMutex);
        functionStats.clear();
        memoryStats = MemoryStats();
    }
    
    // Destructor
    ~DebugFramework() {
        if (logFile.is_open()) {
            logFile.close();
        }
    }
};

// Initialize static members
DebugFramework* DebugFramework::instance = nullptr;
std::mutex DebugFramework::instanceMutex;

// Enhanced Timer class that works with the debug framework
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
    Timer(const std::string& name = "", bool autoStart = true) 
        : mName(name) {
        if (autoStart) {
            start();
        }
    }
    
    // Start the timer
    void start() {
        if (!mIsRunning) {
            mTimeStart = clock_type::now();
            mIsRunning = true;
        }
    }
    
    // Stop the timer and return elapsed time since last start
    long long stop() {
        if (!mIsRunning) {
            return 0;
        }
        
        time_point timeEnd = clock_type::now();
        long long elapsedMicroseconds = std::chrono::duration_cast<duration>(timeEnd - mTimeStart).count();
        mTotalMicroseconds += elapsedMicroseconds;
        mIsRunning = false;
        
        return elapsedMicroseconds;
    }
    
    // Reset timer and optionally start it again
    void reset(bool autoStart = true) {
        mTotalMicroseconds = 0;
        mIsRunning = false;
        if (autoStart) {
            start();
        }
    }
    
    // Get total elapsed time without stopping the timer
    long long elapsed() const {
        if (!mIsRunning) {
            return mTotalMicroseconds;
        }
        
        time_point timeNow = clock_type::now();
        return mTotalMicroseconds + 
               std::chrono::duration_cast<duration>(timeNow - mTimeStart).count();
    }
    
    // Get total accumulated time
    long long total() const {
        return mTotalMicroseconds;
    }
    
    // Set timer name
    void setName(const std::string& name) {
        mName = name;
    }
    
    // Get timer name
    const std::string& getName() const {
        return mName;
    }
};

// Enhanced ScopeTimer that integrates with debug framework
class ScopeTimer {
private:
    Timer mTimer;
    std::string mName;
    DebugLevel mLogLevel;

public:
    ScopeTimer(const std::string& name, DebugLevel logLevel = DebugLevel::DEBUG) 
        : mName(name), mLogLevel(logLevel) {
        mTimer.start();
        // Track function entry
        DebugFramework::getInstance().enterFunction(mName);
    }
    
    ~ScopeTimer() {
        long long elapsed = mTimer.stop();
        // Track function exit with duration
        DebugFramework::getInstance().exitFunction(mName, elapsed);
        // Optional logging at specified level
        DebugFramework::getInstance().log(mLogLevel, mName + " took " + std::to_string(elapsed) + " microseconds");
    }
};

// Memory tracker for RAII-style memory tracking
class MemoryTracker {
private:
    std::string mFunctionName;
    size_t mBytes;

public:
    MemoryTracker(const std::string& functionName, size_t bytes) 
        : mFunctionName(functionName), mBytes(bytes) {
        DebugFramework::getInstance().trackAllocation(mFunctionName, mBytes);
    }
    
    ~MemoryTracker() {
        DebugFramework::getInstance().trackDeallocation(mFunctionName, mBytes);
    }
};

// Macros for easy usage
#define LOG_TRACE(msg) DebugFramework::getInstance().log(DebugLevel::TRACE, msg)
#define LOG_DEBUG(msg) DebugFramework::getInstance().log(DebugLevel::DEBUG, msg)
#define LOG_INFO(msg) DebugFramework::getInstance().log(DebugLevel::INFO, msg)
#define LOG_WARN(msg) DebugFramework::getInstance().log(DebugLevel::WARN, msg)
#define LOG_ERROR(msg) DebugFramework::getInstance().log(DebugLevel::ERROR, msg)
#define LOG_CRITICAL(msg) DebugFramework::getInstance().log(DebugLevel::CRITICAL, msg)

#define TIME_FUNCTION() ScopeTimer scopeTimer(__func__)
#define TIME_BLOCK(name) ScopeTimer scopeTimer(name)
#define TRACK_MEMORY(bytes) MemoryTracker memTracker(__func__, bytes)

// Example of a custom memory allocator wrapper that could be used
template<typename T>
class DebugAllocator {
public:
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

// Example usage
void exampleFunction() {
    TIME_FUNCTION();  // Automatically times this function
    
    LOG_INFO("Starting example function");
    
    // Track a memory allocation
    {
        TRACK_MEMORY(1024);
        // Simulating allocation of 1KB
        char* buffer = new char[1024];
        
        // Do some work
        TIME_BLOCK("Critical section");
        for (int i = 0; i < 1000; i++) {
            buffer[i % 1024] = i % 256;
        }
        
        // Clean up
        delete[] buffer;
    }  // Memory tracker goes out of scope here
    
    LOG_INFO("Example function complete");
}