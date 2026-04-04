#pragma once

#include "ModbusScanner.hpp"
#include <chrono>
#include <string>

using namespace std;

class DataPoint {
public:
    DataPoint() = default;

    uint16_t slave_id = 0;
    bool is_active = false;
    float dc_voltage = 0;
    float dc_current = 0;
    float dc_power = 0;
    float ac_voltage = 0;
    float ac_current = 0;
    float ac_power = 0;
    uint16_t error_code = 0;
    string device_name = "";

    bool synced = false;
    std::chrono::system_clock::time_point timestamp;
};
