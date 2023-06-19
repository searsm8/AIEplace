#include "aieplace.h"
#include "database.h"

int main()
{
    cout << "Hello aieplace!" << endl;
    AIEplace::DataBase db = AIEplace::DataBase();
    db.readLEF();
    db.readDEF();
//  test1(filename);
//	bool flag = aieplace::readLef(db);

}