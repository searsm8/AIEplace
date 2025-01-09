// Logger.h
// Simple logger for debugging and managing data printed.
#ifndef LOGGER_H
#define LOGGER_H

#include "Common.h"
#include <unordered_set>
#include <tabulate/table.hpp>
#include <tabulate/markdown_exporter.hpp>

using namespace tabulate;
using std::string;

namespace AIEplace {

static std::unordered_set<string> logging_keys;
static std::map<string, Color> string_colors;

inline void activate_logging_key(string key)
{ logging_keys.insert(key); }

inline void deactivate_logging_key(string key)
{ logging_keys.erase(key); }

void setup_logging();
bool log(string key, Table t);
bool log(string key, string msg);
bool log_info(string msg);
bool log_debug(string msg);
bool log_warning(string msg);
bool log_error(string msg);
bool log_data(string msg);
bool log_info(Table t);
bool log_debug(Table t);
bool log_warning(Table t);
bool log_error(Table t);
bool log_data(Table t);
void log_space();
Color getColor(string key);

void export_markdown(Table t, fs::path dir);
}

#endif