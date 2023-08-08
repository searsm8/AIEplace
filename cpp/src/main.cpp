#include "AIEplace.h"

int main(int argc, char *argv[])
{
    AIEplace::Placer::printVersionInfo();

    // Use argument for benchmark path if given, otherwise look in current path.
    if (argc < 2 )
    {
        cout << "\t!!! No design directory has been specified !!!" << endl;
        cout << "Usage: ./AIEplace.exe <PATH_TO_DESIGN_DIRECTORY>" << endl;
        exit(1);
    }
        
    fs::path design_input_dir{argv[1]};

    // Add ability to give design path as input

    AIEplace::Placer placer(design_input_dir);

    //db.printInfo(); 
    // Print DataBase info
    //db.printNodes();
    //placer.db.printNets();
    //db.printNetsBySize();

    placer.run();

    return 0;
}
