// Logger.cpp

#include "Logger.h"
#include <cctype>
#include <unistd.h>

using namespace tabulate; // table types, scoped to this .cpp (not leaked via Logger.h)

// Initialize static members of Logger
LogLevel Logger::console_level = LogLevel::INFO;
bool Logger::console_color = false;
std::unordered_set<string> Logger::custom_keys;
std::ofstream Logger::report_file;
std::vector<string> Logger::report_backlog;
std::unordered_map<string, Logger::FunctionStatBlock> Logger::function_stats_map;

namespace {

// Every line is "<tag right-aligned in TAG_WIDTH><TAG_SEP><message>"; continuation lines of a
// multi-line message get a blank prefix of the same width so tables stay column-aligned.
// Plain stream writes, not a tabulate Table per line: the table renderer padded every line to
// its cell width (trailing whitespace on every line of a piped log) and emitted eight ANSI
// escapes per line on a TTY.
constexpr int TAG_WIDTH = 10;
constexpr const char* TAG_SEP = "  ";
constexpr int PREFIX_WIDTH = TAG_WIDTH + 2; // TAG_SEP length

constexpr const char* ANSI_RESET = "\033[0m";

const char* ansiForLevel(LogLevel level)
{
    switch (level) {
        case LogLevel::TRACE:    return "\033[90m"; // grey
        case LogLevel::DEBUG:    return "\033[35m"; // magenta
        case LogLevel::DETAIL:   return "\033[36m"; // cyan
        case LogLevel::ITER:     return "\033[34m"; // blue
        case LogLevel::INFO:     return "\033[32m"; // green
        case LogLevel::WARNING:  return "\033[33m"; // yellow
        case LogLevel::ERROR:    return "\033[31m"; // red
        case LogLevel::CRITICAL: return "\033[1;31m"; // bold red
        default:                 return "\033[0m";
    }
}

const char* tagForLevel(LogLevel level)
{
    switch (level) {
        case LogLevel::TRACE:    return "TRACE";
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::DETAIL:   return "DETAIL";
        case LogLevel::ITER:     return "ITER";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARNING:  return "WARNING";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default:                 return "";
    }
}

void rstrip(string& text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
}

/// @brief Wrap only the tag in one escape pair, leaving the message uncolored.
string colorize(const string& plain, const char* ansi)
{
    size_t tag_end = std::min<size_t>(TAG_WIDTH, plain.size());
    return string(ansi) + plain.substr(0, tag_end) + ANSI_RESET + plain.substr(tag_end);
}

} // namespace

void Logger::setup_logging(LogLevel console)
{
    console_level = console;
    // ANSI only on a real terminal: a piped run (DSE sweep, nohup log) gets clean text.
    console_color = isatty(fileno(stdout)) != 0;

    // CUSTOM LOGGING KEYS — named channels outside the severity scale, for output that is not
    // "more or less severe" than anything, just optional. Off by default: they still reach the
    // run report, which is where this kind of detail belongs.
    //Logger::activate_logging_key("profiling"); // per-function timing table (also written to function_statistics.md)
}

/// @brief Open the run's full-detail report and flush everything logged before it existed.
void Logger::openReport(fs::path dir, string filename)
{
    report_file.open(dir.append(filename));
    if (!report_file.is_open()) {
        log_warning("Could not open run report: " + dir.string());
        report_backlog.clear();
        return;
    }
    for (const string& line : report_backlog)
        report_file << line << "\n";
    report_file.flush();
    report_backlog.clear();
}

/// @brief Flatten a message to text; tabulate tables render to their multi-line box form.
string Logger::render(const MsgType& msg)
{
    if (const auto* s  = std::get_if<std::string>(&msg))      return *s;
    if (const auto* c  = std::get_if<const char*>(&msg))      return string(*c);
    if (const auto* sv = std::get_if<std::string_view>(&msg)) return string(*sv);
    return std::get<Table>(const_cast<MsgType&>(msg)).str(); // Table::str() is non-const
}

void Logger::emit(const string& tag, const char* ansi, const string& text,
                  bool to_console, bool to_report)
{
    std::istringstream message_lines(text);
    string line;
    bool is_first_line = true;

    while (std::getline(message_lines, line)) {
        std::ostringstream prefixed;
        if (is_first_line) prefixed << std::setw(TAG_WIDTH) << std::right << tag << TAG_SEP;
        else               prefixed << string(PREFIX_WIDTH, ' ');
        prefixed << line;
        is_first_line = false;

        string plain = prefixed.str();
        rstrip(plain); // no trailing whitespace, whatever the tag/message widths are

        if (to_console)
            std::cout << (console_color ? colorize(plain, ansi) : plain) << "\n";
        if (to_report) {
            if (report_file.is_open()) report_file << plain << "\n";
            else                       report_backlog.push_back(plain);
        }
    }

    if (to_console)
        std::cout.flush(); // stdout may not be line-buffered (piped/non-tty); force it so output
                           // appears as it happens instead of batching until the process exits —
                           // easy to miss now that per-iteration output volume is low.
    if (to_report && report_file.is_open())
        report_file.flush(); // the report must survive a crash mid-run
}

bool Logger::log(LogLevel level, const MsgType& msg)
{
    bool to_console = level >= console_level;
    bool to_report  = level >= REPORT_LEVEL;
    if (!to_console && !to_report) return false;

    emit(tagForLevel(level), ansiForLevel(level), render(msg), to_console, to_report);
    return to_console;
}

/// @brief Log on a named channel outside the severity scale. Console output is opt-in per key;
///        the report always takes it.
bool Logger::log_key(const string& key, const MsgType& msg)
{
    bool to_console = isKeyActive(key) && console_level <= LogLevel::INFO; // never in quiet mode
    emit(key, "\033[33m", render(msg), to_console, true); // yellow
    return to_console;
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

double Logger::getFunctionTime(const std::string& func_name) {
    if (function_stats_map.find(func_name) != function_stats_map.end())
        return function_stats_map[func_name].total_time;
    return 0.0;
}

void Logger::updateFunctionStats(string func_name, long long func_time)
{
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
    for(const auto& item : function_stats_map) {
        const FunctionStatBlock &s = item.second;
        long avg = std::accumulate(s.recent_times.begin(), s.recent_times.end(), 0) / s.recent_times.size();
        top.add_row(RowStream{} << item.first << s.call_count << s.total_time << s.min_time << s.max_time << avg);
    }

    log_key("profiling", top);
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