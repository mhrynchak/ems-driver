#pragma once

#include <vector>
#include <string>
#include <iostream>
#include <modbus/modbus.h>
#include <unistd.h>
#include <iomanip>
#include <memory>
#include <atomic>

#include "DataPoint.hpp"

// Forward declaration to avoid circular dependency
class Context;

using namespace std;

class ModbusScanner {
public:
    ModbusScanner(std::shared_ptr<Context> ctx, const string& host = "127.0.0.1", int port = 502, int maxSlavesNum = 100);
    ~ModbusScanner();

    bool Connect();
    void printDevicesData();
    bool readInverterData(int id, DataPoint& data);
    vector<int> scanForSlaves();
    void Discover();
    void Run(int interval_seconds = 5);

private:
    std::shared_ptr<Context> ctx_;
    string host;
    int port;
    int maxSlavesNum;
    modbus_t *mb;
    bool firstRun = true;
    vector<int> _activeSlaves;
};
