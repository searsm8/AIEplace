// Logger.h
// Simple logger for debugging and managing data printed.
#ifndef LOGGER_H
#define LOGGER_H

#include <unordered_set>
#include <iostream>
#include <map>
#include <tabulate/table.hpp>
using namespace tabulate;

namespace AIEplace {

static std::unordered_set<std::string> logging_keys;
static std::map<std::string, Color> string_colors;

void setup_logging();
void set_colors();
void inline activate_logging_key(std::string key);
void inline deactivate_logging_key(std::string key);
bool log(std::string key, std::string msg);
bool log_info(std::string msg);
bool log_debug(std::string msg);
bool log_warning(std::string msg);
bool log_error(std::string msg);
void log_table();
void log_iteration_report();
void log_placement_report();
//void log_test();
}

#endif