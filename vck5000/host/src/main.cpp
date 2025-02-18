#include "AIEplace.h"

int main(int argc, char *argv[])
{
    AIEplace::setup_logging();
    AIEplace::Placer::printWelcomeBanner();

    // Use argument for config filepath if given
    std::string config_filepath;
    if (argc > 1)
        config_filepath = argv[1];
    else config_filepath = "host/default_config.json"; // default

    // Instantiate the placer
    AIEplace::Placer placer(config_filepath);

    // Print DataBase info
    placer.db.printInfo(); 
    //placer.db.computeTotalComponentArea();
    //placer.db.printNodes();
    //placer.db.printNets();
    //placer.db.printNetsByDegree();

    placer.run();

    return 0;
}
