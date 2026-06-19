//#include "AIEplace.h"
#include "DataBase.h"
#include "PlacementEngine.h"
#include "Placer.h"
#include "Logger.h"

#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>

int main(int argc, char *argv[])
{

    //Logger::setup_logging();

    //std::string config_filepath = (argc > 1) ? argv[1] : "host/run_config.json"; // default

    // Instantiate the placer
    //AIEplace::Placer placer(config_filepath);
    AIEplace::DataBase db("host/benchmarks/ispd2015/mgc_des_perf_a", 1024);

    AIEplace::VCK5000PlacementEngine vck5000(db, std::make_unique<AIEplace::MockFPGADriver>());
    AIEplace::CPUPlacementEngine cpu(db);

    std::string config_filepath = (argc > 1) ? argv[1] : "host/run_config.json"; // default

//    AIEplace::HyperParameters params;
//#ifdef XRT
//    AIEplace::Placer placer(db, cpu, vck5000);
//#else
//    AIEplace::Placer placer(db, cpu);
//#endif

    // Print DataBase info
    db.printInfo();
    Logger::export_Density_Bins(db.die_, "output/", 0);
    //placer.db.computeTotalComponentArea();
    //placer.db.printNodes();
    //placer.db.printNets();
    //placer.db.printNetsByDegree();

    // Run the placer
//    placer.run();

//    placer.plotHistories();
//    placer.printFinalResults();

    return 0;
}
