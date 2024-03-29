#include "AIEplace.h"

int main(int argc, char *argv[])
{
    AIEplace::Placer::printVersionInfo();

    // Use argument for benchmark path if given, otherwise look in current path.
    if (argc < 3 )
    {
        cout << "\t!!! No design directory has been specified !!!" << endl;
        cout << "Usage: ./AIEplace.exe <PATH_TO_DESIGN_DIRECTORY> <PATH_TO_XCLBIN>" << endl;
        exit(1);
    }
        
    fs::path design_input_dir{argv[1]};
    std::string xclbin_file{argv[2]};

    AIEplace::Placer placer(design_input_dir, xclbin_file);

    // Print DataBase info
    placer.db.printInfo(); 
    placer.db.computeTotalComponentArea();
    //placer.db.printNodes();
    //placer.db.printNets();
    //placer.db.printNetsByDegree();

    placer.run();

    return 0;
}
