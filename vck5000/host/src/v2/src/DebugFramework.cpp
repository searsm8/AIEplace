#include "DebugFramework.h"

// Initialize static members of DebugFramework
DebugFramework* DebugFramework::instance = nullptr;
std::mutex DebugFramework::instanceMutex;

// DebugFramework implementation
DebugFramework::DebugFramework() {
    if (enableLogging) {
        logFile.open(logFilename, std::ios::out | std::ios::app);
    }
}

DebugFramework& DebugFramework::getInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (instance == nullptr) {
        instance = new DebugFramework();
    }
    return *instance;
}

void DebugFramework::setDebugLevel(DebugLevel level) {
    currentLevel = level;
}

void DebugFramework::setLogFile(const std::string& filename) {
    if (logFile.is_open()) {
        logFile.close();
    }
    logFilename = filename;
    logFile.open(logFilename, std::ios::out | std::ios::app);
}

void DebugFramework::enableFeature(const std::string& feature, bool enable) {
    if (feature == "timing") enableTiming = enable;
    else if (feature == "logging") enableLogging = enable;
    else if (feature == "callstack") enableCallStack = enable;
    else if (feature == "memory") enableMemoryTracking = enable;
}

void DebugFramework::setThreadName(const std::string& name) {
    std::lock_guard<std::mutex> lock(threadMutex);
    threadNames[std::this_thread::get_id()] = name;
}

void DebugFramework::log(DebugLevel level, const std::string& message) {
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

void DebugFramework::enterFunction(const std::string& functionName) {
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

void DebugFramework::exitFunction(const std::string& functionName, long long durationMicroseconds) {
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

void DebugFramework::trackAllocation(const std::string& functionName, size_t bytes) {
    if (!enableMemoryTracking) return;
    
    std::lock_guard<std::mutex> lock(statsMutex);
    memoryStats.currentUsage += bytes;
    memoryStats.peakUsage = std::max(memoryStats.peakUsage, memoryStats.currentUsage);
    memoryStats.allocationsByFunction[functionName] += bytes;
}

void DebugFramework::trackDeallocation(const std::string& functionName, size_t bytes) {
    if (!enableMemoryTracking) return;
    
    std::lock_guard<std::mutex> lock(statsMutex);
    memoryStats.currentUsage = (memoryStats.currentUsage >= bytes) ? 
                               memoryStats.currentUsage - bytes : 0;
}

std::string DebugFramework::generateReport() {
    std::lock_guard<std::mutex> lock(statsMutex);
    
    std::stringstream report;
    report << "===== Debug Framework Report =====\n";
    std::time_t currentTime = std::time(nullptr);
    report << "Generated: " << std::put_time(std::localtime(&currentTime), "%Y-%m-%d %H:%M:%S") << "\n\n";
    
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

void DebugFramework::saveReportToFile(const std::string& filename) {
    std::ofstream reportFile(filename);
    if (reportFile.is_open()) {
        reportFile << generateReport();
        reportFile.close();
    }
}

void DebugFramework::resetStatistics() {
    std::lock_guard<std::mutex> lock(statsMutex);
    functionStats.clear();
    memoryStats = MemoryStats();
}

DebugFramework::~DebugFramework() {
    if (logFile.is_open()) {
        logFile.close();
    }
}

// Timer implementation
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
ScopeTimer::ScopeTimer(const std::string& name, DebugLevel logLevel) 
    : mName(name), mLogLevel(logLevel) {
    mTimer.start();
    // Track function entry
    DebugFramework::getInstance().enterFunction(mName);
}

ScopeTimer::~ScopeTimer() {
    long long elapsed = mTimer.stop();
    // Track function exit with duration
    DebugFramework::getInstance().exitFunction(mName, elapsed);
    // Optional logging at specified level
    DebugFramework::getInstance().log(mLogLevel, mName + " took " + std::to_string(elapsed) + " microseconds");
}

// MemoryTracker implementation
MemoryTracker::MemoryTracker(const std::string& functionName, size_t bytes) 
    : mFunctionName(functionName), mBytes(bytes) {
    DebugFramework::getInstance().trackAllocation(mFunctionName, mBytes);
}

MemoryTracker::~MemoryTracker() {
    DebugFramework::getInstance().trackDeallocation(mFunctionName, mBytes);
}