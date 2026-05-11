//#include "AIEplace.h"
#include "DataBase.h"
#include "Library.h"
#include "Parsers.h"

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
    AIEplace::DataBase db("host/benchmarks/ispd2015/mgc_des_perf_a");

    AIEplace::CellParser cellparser(db.typeLib);
    cellparser.parseFile("host/benchmarks/ispd2015/mgc_des_perf_a/cells.lef");
    db.typeLib.sort(AIEplace::ComponentTypeComparator{});
    db.typeLib.lock();

    AIEplace::FloorplanParser floorplanparser(db.componentLib, db.typeLib);
    floorplanparser.parseFile("host/benchmarks/ispd2015/mgc_des_perf_a/floorplan.def");
    db.componentLib.sort(AIEplace::ComponentComparator{});
    db.componentLib.lock();

    AIEplace::print_component_type_library(db.typeLib);
    AIEplace::print_component_library(db.componentLib);

    // Print DataBase info
    //placer.db.printInfo(); 
    //placer.db.computeTotalComponentArea();
    //placer.db.printNodes();
    //placer.db.printNets();
    //placer.db.printNetsByDegree();

    // Run the placer 
    //placer.run();

    //placer.plotHistories();
    //placer.printFinalResults();

    return 0;
}
