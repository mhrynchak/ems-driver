#pragma once

#include <vector>
#include <string>
#include <iostream>
#include <modbus/modbus.h>
#include <unistd.h>
#include <iomanip>
#include <memory>
#include <atomic>
#include "IDeviceDriver.hpp"
#include <map>

#include "DataPoint.hpp"

// Forward declaration to avoid circular dependency
class Context;

using namespace std;

class ModbusScanner {
public:
    ModbusScanner(
        std::shared_ptr<Context> ctx,
        const string& host = "127.0.0.1",
        int port = 502,
        int maxSlavesNum = 100,
        const string& endpointName = "default",
        int cacheKeyOffset = 0,
        int offlineFailureThreshold = 3);
    ~ModbusScanner();

    bool Connect();
    void registerDriver(const std::string& name, std::shared_ptr<IDeviceDriver> driver);
    void printDevicesData();
    bool readInverterData(int id, DataPoint& data); // This will now delegate to the specific driver
    vector<int> scanForSlaves();
    void Discover();
    void Run(int interval_seconds = 5);

private:
    void closeConnection();
    bool reconnect(const std::string& reason);
    std::shared_ptr<IDeviceDriver> resolveDriver(int id);
    bool isSunSpecDevice(int id);
    bool isHuaweiDevice(int id);

    std::shared_ptr<Context> ctx_;
    std::map<int, std::shared_ptr<IDeviceDriver>> deviceDrivers_; // Map slave ID to specific driver instance
    std::map<std::string, std::shared_ptr<IDeviceDriver>> driverRegistry_; // Map driver name to a driver factory/prototype
    string host;
    int port;
    int maxSlavesNum;
    modbus_t *mb;
    bool firstRun = true;
    vector<int> _activeSlaves;
    string endpointName_;
    int cacheKeyOffset_;
    uint32_t offlineFailureThreshold_;
};
