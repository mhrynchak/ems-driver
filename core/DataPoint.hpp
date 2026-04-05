#pragma once

#include "ModbusScanner.hpp"
#include <chrono>
#include <string>

using namespace std;

enum class ConnectivityState {
    Connecting,
    Online,
    Offline
};

class DataPoint {
public:
    DataPoint() = default;

    uint16_t slave_id = 0;
    uint16_t source_slave_id = 0;
    string source_endpoint = "";
    bool is_active = false;
    float dc_voltage = 0;
    float dc_current = 0;
    float dc_power = 0;
    float ac_voltage = 0;
    float ac_current = 0;
    float ac_power = 0;
    bool has_meter_telemetry = false;
    float meter_active_power = 0;
    bool has_battery_telemetry = false;
    float battery_soc = 0;
    float battery_power = 0;
    uint16_t error_code = 0;
    string device_name = "";
    string driver_name = "Unknown";

    bool synced = false;
    std::chrono::system_clock::time_point timestamp;
    std::chrono::system_clock::time_point last_read_attempt;
    bool has_successful_read = false;
    uint32_t consecutive_read_failures = 0;
    ConnectivityState connectivity_state = ConnectivityState::Connecting;

    // Accumulation fields for periodic aggregation and daily yield tracking.
    double dcPowerSampleSum = 0.0;
    double acPowerSampleSum = 0.0;
    uint64_t sampleCount = 0;
    double dailyYieldKwh = 0.0;
    double lastHourAvgPower = 0.0;  // Last completed hourly average (for dashboard)
};
