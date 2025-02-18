// Logger.cpp

#include "Logger.h"

namespace AIEplace {

void setup_logging()
{
    string_colors["DETAIL"] = Color::cyan;
    string_colors["INFO"] = Color::green;
    string_colors["DEBUG"] = Color::magenta;
    string_colors["WARNING"] = Color::yellow;
    string_colors["ERROR"] = Color::red;
    string_colors["DATA"] = Color::blue;

    // DEFAULT LOGGING KEYS
    activate_logging_key("DETAIL"); // Used to give detail on things that most people won't care about. Usually off.
    activate_logging_key("INFO");   // Used to give info on things that some people might care about. Usually on.
    activate_logging_key("DATA");   // Used to give data on things that most people will care about. Always on.
    activate_logging_key("DEBUG");  // Used only by developers for debugging!
    activate_logging_key("WARNING");// Something bad has happened!
    activate_logging_key("ERROR");  // Something VERY bad has happened!

    // CUSTOM LOGGING KEYS
    //activate_logging_key("packets"); // Used in DataBase.cpp for packet initialization
    activate_logging_key("dbinfo");
    //activate_logging_key("comms");
    //activate_logging_key("bins");
    //activate_logging_key("overlap");
    //activate_logging_key("function"); // Used to log when important functions are called
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
        return true;
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

bool log_detail(std::string msg)
{ return log("DETAIL", msg); }
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

bool log_detail(Table t)
{ return log("DETAIL", t); }
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

void export_markdown(Table t, fs::path dir)
{
    // Use exporter
    MarkdownExporter exporter;
    auto markdown = exporter.dump(t);

    // Write to file
    std::ofstream out_file;
    out_file.open(dir.append("statistics.md"));
    out_file << markdown;
    out_file.close();
}

// used for debugging, enables easy comparison of CPU and AIE results
// export intermdiate results of computations such as density or partials terms
void export_intermediate_results(Grid& grid, fs::path dir, int iter)
{
    std::ofstream out_file;
    out_file.open(dir.append("intermed_eField.dat"), std::ios_base::app);
    out_file << endl << "Iteration: " << iter << endl;
    out_file << "eField.x" << endl;
    for (int x = 0; x < grid.getBinsPerRow(); x++) {
        out_file << "row " << x << ": ";
        for (int y = 0; y < grid.getBinsPerCol(); y++) {
            out_file << grid.getBin(x, y).eField.x << " ";
        }
        out_file << endl;
    }

    out_file << endl << "eField.y" << endl;
    for (int x = 0; x < grid.getBinsPerRow(); x++) {
        out_file << "row " << x << ": ";
        for (int y = 0; y < grid.getBinsPerCol(); y++) {
            out_file << grid.getBin(x, y).eField.y << " ";
        }
        out_file << endl;
    }

    out_file.close();
}

void append_csv(StatBlock &stats)
{
    // Write to CSV file
    std::ofstream out_file;
    out_file.open("results/run_statistics.csv", std::ios_base::app);
    out_file << stats.name << ", ";
    out_file << stats.iteration_count << ", ";
    out_file << std::scientific;
    out_file << stats.final_hpwl<< ", ";
    out_file << std::fixed << std::setprecision(3);
    out_file << stats.final_learning_rate<< ", ";
    out_file << stats.prgm_runtime << ", ";
    out_file << stats.db_IO_time << ", ";
    out_file << stats.algo_time << ", ";
    out_file << endl;
    out_file.close();
}

}