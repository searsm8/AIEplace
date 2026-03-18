#include "AIEplace.h"

int main(int argc, char *argv[])
{

    Logger::setup_logging();

    std::string config_filepath = (argc > 1) ? argv[1] : "host/run_config.json"; // default

    // Instantiate the placer
    AIEplace::Placer placer(config_filepath);

    // Print DataBase info
    placer.db.printInfo(); 
    //placer.db.computeTotalComponentArea();
    //placer.db.printNodes();
    //placer.db.printNets();
    //placer.db.printNetsByDegree();

    // Run the placer 
    placer.run();

    placer.plotHistories();
    placer.printFinalResults();

    return 0;
}
