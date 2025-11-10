#include "Database.hpp"
#include <iostream>
#include "ModbusScanner.hpp"

int main(int argc, char* argv[]) {
    string host = "127.0.0.1";
    int port = 502;

    std::string dbPath = "database.db";
    Database database(dbPath);

    ModbusScanner scanner(host, port);

    if (!scanner.Connect()) {
        return -1;
    }

    scanner.Run();

    return 0;
}
