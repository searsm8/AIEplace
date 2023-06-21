#include "AIEplace.h"
#include "Database.h"

int main()
{
    cout << "Hello aieplace!" << endl;
    AIEplace::DataBase db = AIEplace::DataBase();
    db.readLEF();
    db.readDEF();
    db.printNodes();
    //db.printNets();
    db.printStats();

}