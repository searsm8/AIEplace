// Logger.cpp

#include "Logger.h"

namespace AIEplace {

void setup_logging()
{
    string_colors["INFO"] = Color::green;
    string_colors["DEBUG"] = Color::magenta;
    string_colors["WARNING"] = Color::yellow;
    string_colors["ERROR"] = Color::red;
    string_colors["DATA"] = Color::blue;

    // Default logging keys
    activate_logging_key("INFO");
    activate_logging_key("DEBUG");
    activate_logging_key("WARNING");
    activate_logging_key("ERROR");
    activate_logging_key("DATA");

    // Custom logging keys
    //activate_logging_key("packets"); // Used in DataBase.cpp for packet initialization
    activate_logging_key("dbinfo");
    //activate_logging_key("comms");
    //activate_logging_key("bins");
    activate_logging_key("overlap");
    activate_logging_key("function"); // Used to log when important functions are called
}

bool log(std::string key, Table t)
{
    // if the key is in the active key set, print it
    if(logging_keys.find(key) == logging_keys.end())
        return false;
    else {
        Table top;
        top.add_row({"["+key+"]", t});
        top.format().hide_border().font_align(FontAlign::center);
        top.column(0).format()
            .width(12)
            .font_style({FontStyle::bold, FontStyle::italic})
            .font_align(FontAlign::right)
            .font_color(getColor(key));
        top.print(std::cout);
    }
}
bool log(std::string key, std::string msg)
{
    // if the key is in the active key set, print it
    if(logging_keys.find(key) == logging_keys.end())
        return false;
    else {
        Table table;
        table.add_row({"["+key+"]", msg});

        table.format().hide_border();

        table.column(0).format()
            .width(12)
            .font_style({FontStyle::bold, FontStyle::italic})
            .font_align(FontAlign::right)
            .font_color(getColor(key));

        table.column(1).format()
            .border_left(":");

        table.print(std::cout);
        return true;
    }
}

bool log_info(std::string msg)
{ return log("INFO", msg); }
bool log_debug(std::string msg)
{ return log("DEBUG", msg); }
bool log_warning(std::string msg)
{ return log("WARNING", msg); }
bool log_error(std::string msg)
{ return log("ERROR", msg); }
bool log_data(std::string msg)
{ return log("DATA", msg); }

bool log_info(Table t)
{ return log("INFO", t); }
bool log_debug(Table t)
{ return log("DEBUG", t); }
bool log_warning(Table t)
{ return log("WARNING", t); }
bool log_error(Table t)
{ return log("ERROR", t); }
bool log_data(Table t)
{ return log("DATA", t); }

void log_space()
{
    std::cout << std::endl;
}

Color getColor(std::string key)
{
        if ( string_colors.find(key) == string_colors.end() )
            return Color::white;
        else return string_colors[key];

}

void export_markdown(Table t)
{
    // Use exporter
    MarkdownExporter exporter;
    auto markdown = exporter.dump(t);

    // Write to file
    std::ofstream out_file;
    out_file.open(get_filename());
    out_file << markdown;
    out_file.close();
}

std::string get_filename()
{
    fs::path filepath = "results";
    fs::create_directories(filepath); // make sure this subdirectory exists

    std::time_t time = std::time(0);   // get time now
    std::tm* now = std::localtime(&time);

    std::stringstream stream;
    stream << "results/" << now->tm_hour << ":" << now->tm_min << "_" << now->tm_yday
            << "_" << std::to_string(now->tm_year+1900) << ".md";
    return stream.str();
}

}