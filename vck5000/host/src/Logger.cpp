// Logger.cpp

#include "Logger.h"

namespace AIEplace {

void setup_logging()
{
    string_colors["INFO"] = Color::green;
    string_colors["DEBUG"] = Color::magenta;
    string_colors["WARNING"] = Color::yellow;
    string_colors["ERROR"] = Color::red;

    activate_logging_key("INFO");
    activate_logging_key("DEBUG");
    activate_logging_key("WARNING");
    activate_logging_key("ERROR");
}

void inline activate_logging_key(std::string key)
{ logging_keys.insert(key); }

void inline deactivate_logging_key(std::string key)
{ logging_keys.erase(key); }

bool log(std::string key, std::string msg)
{
    // if the key is in the active key set, print it
    if(logging_keys.find(key) == logging_keys.end())
        return false;
    else {
        Table table;
        table.add_row({key, msg});

        table.format()
            .border_top(" ")
            .border_bottom(" ")
            .border_left("")
            .border_right("")
            .corner("");

        table.column(0).format()
            .width(10)
            .font_style({FontStyle::bold, FontStyle::italic})
            .font_align(FontAlign::right)
            .font_color(string_colors[key]);

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

// Function to test and play around with tabulate Tables...
void log_table()
{
    Table universal_constants;

    universal_constants.add_row({"Quantity", "Value"});
    universal_constants.add_row({"Characteristic impedance of vacuum", "376.730 313 461... Ω"});
    universal_constants.add_row({"Electric constant\n(permittivity of free space)", "8.854 187 817... × 10⁻¹²F·m⁻¹"});
    universal_constants.add_row({"Magnetic constant\n(permeability of free space)", "4π × 10⁻⁷ N·A⁻² = 1.2566 370 614... × 10⁻⁶ N·A⁻²"});
    universal_constants.add_row({"Gravitational constant (Newtonian constant of gravitation)", "6.6742(10) × 10⁻¹¹m³·kg⁻¹·s⁻²"});
    universal_constants.add_row({"Planck's constant", "6.626 0693(11) × 10⁻³⁴ J·s"});
    universal_constants.add_row({"Dirac's constant", "1.054 571 68(18) × 10⁻³⁴ J·s"});
    universal_constants.add_row({"Speed of light in vacuum", "299 792 458 m·s⁻¹"});

// format the whole table
  universal_constants.format()
    .width(50)
    .font_style({FontStyle::bold})
    .border_top(" ")
    .border_bottom(" ")
    .border_left(" ")
    .border_right(" ")
    .corner(" ");

// format the first row
  universal_constants[0].format()
    .padding_top(1)
    .padding_bottom(1)
    .font_align(FontAlign::center)
    .font_style({FontStyle::underline})
    .font_background_color(Color::red);

// format the right column
  universal_constants.column(1).format()
    .font_color(Color::blue);

  universal_constants.column(1)[3].format()
    .font_color(Color::red);

    std::cout << universal_constants << std::endl;
    exit(0);
}

void log_iteration_report()
{

}

void log_placement_report()
{

}

//void log_test()
//{
//    log_info("This is some info.")
//    log_debug("This is a debug msg.");
//    log_warning("This is a warning.");
//    log_error("THIS IS AN ERROR.");
//    exit(0);
//}

}