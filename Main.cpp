#include "ModbusScanner.hpp"

int main(int argc, char* argv[]) {
    string host = "127.0.0.1";
    int port = 502;

    ModbusScanner scanner(host, port);

    if (!scanner.Connect()) {
        return -1;
    }

    scanner.Run();

    return 0;
}
