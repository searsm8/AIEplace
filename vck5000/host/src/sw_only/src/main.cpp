/**
 * @file main.cpp
 * @brief Entry point: construct the Placer from the config file, run placement, emit results.
 */
#include "AIEplace.h"

int main(int argc, char *argv[])
{

    std::string config_filepath = (argc > 1) ? argv[1] : "host/src/sw_only/run_config.toml"; // default

    AIEplace::Placer placer(config_filepath);
    placer.db.printInfo(); 
    placer.run();

    placer.printFinalResults();

    return 0;
}
