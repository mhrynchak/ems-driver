#include "ModbusScanner.hpp"
#include "../Context.hpp"
#include <chrono>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <ctime>
#include <mutex>
#include <sstream>

namespace {

std::mutex logMutex;

void logInfo(const std::string& message) {
    const std::lock_guard<std::mutex> lock(logMutex);
    std::cout << message << std::endl;
}

void logError(const std::string& message) {
    const std::lock_guard<std::mutex> lock(logMutex);
    std::cerr << message << std::endl;
}

void logInfoBlock(const std::string& message) {
    const std::lock_guard<std::mutex> lock(logMutex);
    std::cout << message;
    if (message.empty() || message.back() != '\n') {
        std::cout << '\n';
    }
    std::cout.flush();
}

std::string connectivityLabel(const DataPoint& dataPoint) {
    if (dataPoint.connectivity_state == ConnectivityState::Offline) {
        return "Offline";
    }

    if (dataPoint.connectivity_state == ConnectivityState::Connecting) {
        return "Connect";
    }

    if (dataPoint.consecutive_read_failures > 0 || dataPoint.error_code != 0 || !dataPoint.is_active) {
        return "Warn";
    }

    return "Online";
}

bool hasVisibleTelemetry(const DataPoint& dataPoint) {
    return dataPoint.has_successful_read && dataPoint.connectivity_state != ConnectivityState::Offline;
}

std::string formatSummaryMetric(const DataPoint& dataPoint, float value) {
    if (!hasVisibleTelemetry(dataPoint)) {
        return "N/A";
    }

    std::ostringstream stream;
    stream << fixed << setprecision(1) << value;
    return stream.str();
}

bool isConnectionError(int errorCode) {
    switch (errorCode) {
        case EPIPE:
        case ECONNRESET:
        case ECONNABORTED:
        case ENOTCONN:
        case ETIMEDOUT:
        case ECONNREFUSED:
        case EBADF:
            return true;
        default:
            return false;
    }
}

}  // namespace

ModbusScanner::ModbusScanner(
        std::shared_ptr<Context> ctx,
        const string& host,
        int port,
        int scanStartSlaveId,
        int scanEndSlaveId,
        const vector<int>& slaveIds,
        const string& endpointName,
        int cacheKeyOffset,
        int offlineFailureThreshold)
        : ctx_(std::move(ctx)),
            host(host),
            port(port),
            scanStartSlaveId_(scanStartSlaveId),
            scanEndSlaveId_(scanEndSlaveId),
            slaveIds_(slaveIds),
            mb(nullptr),
            endpointName_(endpointName),
            cacheKeyOffset_(cacheKeyOffset),
            offlineFailureThreshold_(std::max(1, offlineFailureThreshold)) {}

ModbusScanner::~ModbusScanner() {
    closeConnection();
}

void ModbusScanner::closeConnection() {
    if (!mb) {
        return;
    }

    modbus_close(mb);
    modbus_free(mb);
    mb = nullptr;
}

bool ModbusScanner::Connect() {
    closeConnection();

    const std::string portString = std::to_string(port);
    mb = modbus_new_tcp_pi(host.c_str(), portString.c_str());
    if (mb == nullptr) {
        logError("Failed to create Modbus context");
        return false;
    }

    modbus_set_response_timeout(mb, 0, 50000);  // 50ms timeout (0 sec, 50000 microsec)

    if (modbus_connect(mb) == -1) {
        logError(
            "Failed to connect to " + host + ":" + std::to_string(port) +
            " - " + modbus_strerror(errno));
        closeConnection();
        return false;
    }

    logInfo("Connected to Modbus server at " + host + ":" + std::to_string(port));
    return true;
}

bool ModbusScanner::reconnect(const std::string& reason) {
    logError(
        "Reconnecting Modbus endpoint '" + endpointName_ + "' at " +
        host + ":" + std::to_string(port) + " after " + reason);

    closeConnection();
    const bool reconnected = Connect();
    if (reconnected) {
        firstRun = true;
    }

    return reconnected;
}

void ModbusScanner::printDevicesData() {
    const auto & devices = ctx_->cache->getInverterCache();
    if (devices.empty()) {
        logInfo("No inverter data available.");
        return;
    }

    std::ostringstream output;
    output << "\n--- INVERTER DISCOVERY AND DATA COLLECTION ---\n";
    output << "Endpoint: " << endpointName_ << '\n';

    size_t endpointDeviceCount = 0;
    for (const auto& [id, inv] : devices) {
        if (!inv.source_endpoint.empty() && inv.source_endpoint != endpointName_) {
            continue;
        }
        endpointDeviceCount++;
    }

    output << "Found " << endpointDeviceCount << " active inverters:\n";
    output << string(80, '-') << '\n';

    for (const auto& [id, inv] : devices) {
        if (!inv.source_endpoint.empty() && inv.source_endpoint != endpointName_) {
            continue;
        }
        output << "Inverter: " << inv.device_name << '\n';
    }

    // Summary table
    output << "\n--- SUMMARY TABLE ---\n";
    output << "Slave | DC V | DC A |  DC W  | AC V | AC A |  AC W  | Status\n";
    output << "------|------|------|--------|------|------|--------|--------\n";
    
    int total_dc_power = 0, total_ac_power = 0;
    bool printedAny = false;
    for (const auto& [id, inv] : devices) {
        if (!inv.source_endpoint.empty() && inv.source_endpoint != endpointName_) {
            continue;
        }
        printedAny = true;
        output << setw(5) << id << " |"
               << setw(6) << formatSummaryMetric(inv, inv.dc_voltage) << " |"
               << setw(6) << formatSummaryMetric(inv, inv.dc_current) << " |"
               << setw(8) << formatSummaryMetric(inv, inv.dc_power) << " |"
               << setw(6) << formatSummaryMetric(inv, inv.ac_voltage) << " |"
               << setw(6) << formatSummaryMetric(inv, inv.ac_current) << " |"
               << setw(8) << formatSummaryMetric(inv, inv.ac_power) << " |"
               << setw(8) << connectivityLabel(inv) << '\n';
        
        if (inv.connectivity_state != ConnectivityState::Offline) {
            total_dc_power += inv.dc_power;
            total_ac_power += inv.ac_power;
        }
    }

    if (!printedAny) {
        logInfo("No inverter data available for endpoint '" + endpointName_ + "'.");
        return;
    }
    
    output << string(60, '-') << '\n';
    output << "Total DC Power: " << total_dc_power << " W\n";
    output << "Total AC Power: " << total_ac_power << " W\n";
    output << "System Efficiency: " << fixed << setprecision(1)
           << (total_dc_power > 0 ? (double)total_ac_power / total_dc_power * 100 : 0)
           << "%\n";
    logInfoBlock(output.str());
}

void ModbusScanner::registerDriver(const std::string& name, std::shared_ptr<IDeviceDriver> driver) {
    driverRegistry_[name] = std::move(driver);
    // cout << "Registered driver: " << name << endl;
}

bool ModbusScanner::readInverterData(int id, DataPoint& data) {
    auto driver = resolveDriver(id);
    if (!driver) {
        return false;
    }

    if (!mb && !Connect()) {
        return false;
    }

    if (driver->readData(mb, id, data)) {
        return true;
    }

    const int savedErrno = errno;
    if (!isConnectionError(savedErrno)) {
        return false;
    }

    if (!reconnect("read failure (" + std::string(modbus_strerror(savedErrno)) + ")")) {
        return false;
    }

    return driver->readData(mb, id, data);
}

std::shared_ptr<IDeviceDriver> ModbusScanner::resolveDriver(int id) {
    auto deviceDriver = deviceDrivers_.find(id);
    if (deviceDriver != deviceDrivers_.end()) {
        return deviceDriver->second;
    }

    logError("No specific driver found for slave ID: " + std::to_string(id) + ". Using generic approach.");
    if (driverRegistry_.count("Generic")) {
        return driverRegistry_["Generic"];
    }

    logError("No generic driver registered. Cannot read data for slave " + std::to_string(id));
    return nullptr;
}

bool ModbusScanner::isHuaweiDevice(int id) {
    modbus_set_slave(mb, id);

    uint16_t modelRegs[15]; // Huawei model string at 30000, length 15 registers
    if (modbus_read_registers(mb, 30000, 15, modelRegs) == -1) {
        return false;
    }

    std::string model;
    model.reserve(30);
    for (uint16_t reg : modelRegs) {
        model.push_back(static_cast<char>((reg >> 8) & 0xFF));
        model.push_back(static_cast<char>(reg & 0xFF));
    }

    while (!model.empty() && (model.back() == '\0' || model.back() == ' ')) {
        model.pop_back();
    }

    if (model.empty()) {
        return false;
    }

    std::string upperModel = model;
    std::transform(upperModel.begin(), upperModel.end(), upperModel.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (upperModel.find("SUN2000") != std::string::npos ||
        upperModel.find("HUAWEI") != std::string::npos) {
        logInfo("Huawei device detected! Model: " + model);
        return true;
    }

    return false;
}

bool ModbusScanner::isSunSpecDevice(int id) {
    modbus_set_slave(mb, id);
    uint16_t regs[2]; // Cover 40001 - 40002 registers

    if (modbus_read_registers(mb, 40001, 2, regs) == -1) {
        logError(
            "Failed to read registers for slave " + std::to_string(id) +
            ": " + modbus_strerror(errno));
        return false;
    }

    uint32_t suns = ((uint32_t)regs[0] << 16) | regs[1];

    if (suns == 0x53756e53) {
        logInfo("SunSpec device detected!");
        return true;
    }

    return false;
}

vector<int> ModbusScanner::scanForSlaves() {
    vector<int> activeSlaves = {};
    if (!mb) {
        logError("Not connected to Modbus server");
        return activeSlaves;
    }

    logInfo("Scanning for active Modbus slaves on endpoint '" + endpointName_ + "'...");
    vector<int> candidateSlaveIds;
    if (!slaveIds_.empty()) {
        candidateSlaveIds = slaveIds_;
    } else {
        candidateSlaveIds.reserve(scanEndSlaveId_ - scanStartSlaveId_ + 1);
        for (int id = scanStartSlaveId_; id <= scanEndSlaveId_; ++id) {
            candidateSlaveIds.push_back(id);
        }
    }

    const int totalCandidates = static_cast<int>(candidateSlaveIds.size());
    for (int index = 0; index < totalCandidates; ++index) {
        const int id = candidateSlaveIds[index];
        modbus_set_slave(mb, id);

        uint16_t test_reg;
        int rc = modbus_read_registers(mb, 0, 1, &test_reg); // Read holding register 0

        if (rc == -1) {
            continue;
        }

        activeSlaves.push_back(id);
        std::shared_ptr<IDeviceDriver> assignedDriver = nullptr;

        if (isSunSpecDevice(id) && driverRegistry_.count("SunSpec")) {
            assignedDriver = driverRegistry_["SunSpec"];
        } else if (isHuaweiDevice(id) && driverRegistry_.count("Huawei")) {
            assignedDriver = driverRegistry_["Huawei"];
        }

        std::ostringstream assignment;
        assignment << "Found active device on endpoint '" << endpointName_ << "': " << id << ' ';
        if (assignedDriver) {
            deviceDrivers_[id] = assignedDriver;
            assignment << "(Assigned " << assignedDriver->getDriverName() << " Driver)";
        } else if (driverRegistry_.count("Generic")) { // Fallback to Generic if no specific driver identified
            deviceDrivers_[id] = driverRegistry_["Generic"];
            assignment << "(Assigned Generic Driver)";
        } else {
            assignment << "(No specific or generic driver assigned)";
        }
        logInfo(assignment.str());
        
        // Periodic progress update as a standalone line to avoid cross-thread garbling.
        if ((index + 1) % 10 == 0 || index + 1 == totalCandidates) {
            logInfo(
                "Endpoint '" + endpointName_ + "' scanned " +
                std::to_string(index + 1) + "/" + std::to_string(totalCandidates) +
                " slave candidates");
        }
    }

    logInfo(
        "Scan complete for endpoint '" + endpointName_ + "'. Found " +
        std::to_string(activeSlaves.size()) + " active slaves.");
    return activeSlaves;
}

void ModbusScanner::Discover() {
    auto& inverterCache = ctx_->cache->getInverterCache();
    // Don't clear the cache! We need to preserve sync status
    // inverterCache.clear();

    if (firstRun) {
        logInfo("\n--- INVERTER SCAN ---");
        _activeSlaves = scanForSlaves();
        if (_activeSlaves.empty()) {
            logInfo("No active slaves found. Trying default slave ID 0...");
            _activeSlaves.push_back(0);  // Try default slave ID
        }
    }

    firstRun = false;

    for (int id : _activeSlaves) {
        const auto readAttemptTime = std::chrono::system_clock::now();
        const int cacheKey = cacheKeyOffset_ + id;
        DataPoint newData;
        if (readInverterData(id, newData)) {
            newData.source_slave_id = static_cast<uint16_t>(id);
            newData.source_endpoint = endpointName_;
            newData.slave_id = static_cast<uint16_t>(cacheKey);
            newData.last_read_attempt = readAttemptTime;
            newData.has_successful_read = true;
            newData.consecutive_read_failures = 0;
            newData.connectivity_state = ConnectivityState::Online;
            if (!newData.device_name.empty()) {
                newData.device_name = endpointName_ + " / " + newData.device_name;
            }

            // Check if this is new data or if it has changed
            auto it = inverterCache.find(static_cast<uint16_t>(cacheKey));
            bool isNewData = (it == inverterCache.end());
            bool dataChanged = false;
            
            if (!isNewData) {
                const DataPoint& oldData = it->second;
                // Check if any significant values changed
                dataChanged = (
                    oldData.dc_voltage != newData.dc_voltage ||
                    oldData.dc_current != newData.dc_current ||
                    oldData.dc_power != newData.dc_power ||
                    oldData.ac_voltage != newData.ac_voltage ||
                    oldData.ac_current != newData.ac_current ||
                    oldData.ac_power != newData.ac_power ||
                    oldData.has_meter_telemetry != newData.has_meter_telemetry ||
                    oldData.meter_active_power != newData.meter_active_power ||
                    oldData.has_battery_telemetry != newData.has_battery_telemetry ||
                    oldData.battery_soc != newData.battery_soc ||
                    oldData.battery_power != newData.battery_power ||
                    oldData.error_code != newData.error_code ||
                    oldData.is_active != newData.is_active ||
                    oldData.connectivity_state != newData.connectivity_state ||
                    oldData.consecutive_read_failures != newData.consecutive_read_failures
                );
            }
            
            // Only mark as unsynced if data is new or changed
            if (isNewData || dataChanged) {
                newData.synced = false;  // Needs to be flushed

                // Preserve aggregation state across telemetry refreshes.
                if (!isNewData) {
                    newData.dcPowerSampleSum = it->second.dcPowerSampleSum;
                    newData.acPowerSampleSum = it->second.acPowerSampleSum;
                    newData.sampleCount = it->second.sampleCount;
                    newData.dailyYieldKwh = it->second.dailyYieldKwh;
                    newData.lastHourAvgPower = it->second.lastHourAvgPower;
                }

                inverterCache[static_cast<uint16_t>(cacheKey)] = newData;
                logInfo(
                    "Updated data for endpoint '" + endpointName_ + "', slave " +
                    std::to_string(id) + (dataChanged ? " (changed)" : " (new)"));
            } else {
                // Data hasn't changed, preserve sync status and update timestamp
                newData.synced = it->second.synced;  // Keep existing sync status

                // Preserve aggregation state across telemetry refreshes.
                newData.dcPowerSampleSum = it->second.dcPowerSampleSum;
                newData.acPowerSampleSum = it->second.acPowerSampleSum;
                newData.sampleCount = it->second.sampleCount;
                newData.dailyYieldKwh = it->second.dailyYieldKwh;
                newData.lastHourAvgPower = it->second.lastHourAvgPower;

                inverterCache[static_cast<uint16_t>(cacheKey)] = newData;
                logInfo(
                    "Data unchanged for endpoint '" + endpointName_ +
                    "', slave " + std::to_string(id));
            }

            // Record current sample after cache entry exists/has been refreshed.
            ctx_->cache->recordPowerSample(
                static_cast<uint16_t>(cacheKey),
                newData.dc_power,
                newData.ac_power);
        } else {
            auto it = inverterCache.find(static_cast<uint16_t>(cacheKey));
            DataPoint failedData;

            if (it != inverterCache.end()) {
                failedData = it->second;
            } else {
                failedData.slave_id = static_cast<uint16_t>(cacheKey);
                failedData.source_slave_id = static_cast<uint16_t>(id);
                failedData.source_endpoint = endpointName_;
                failedData.device_name =
                    endpointName_ + " / Device [" + std::to_string(id) + "]";
                failedData.is_active = false;

                const auto driverIt = deviceDrivers_.find(id);
                if (driverIt != deviceDrivers_.end() && driverIt->second) {
                    failedData.driver_name = driverIt->second->getDriverName();
                }
            }

            failedData.last_read_attempt = readAttemptTime;
            failedData.consecutive_read_failures += 1;

            if (failedData.consecutive_read_failures >= offlineFailureThreshold_) {
                failedData.connectivity_state = ConnectivityState::Offline;
            } else if (!failedData.has_successful_read) {
                failedData.connectivity_state = ConnectivityState::Connecting;
            }

            failedData.synced = false;
            inverterCache[static_cast<uint16_t>(cacheKey)] = failedData;

            logInfo(
                "Failed to read data from endpoint '" + endpointName_ +
                "', slave " + std::to_string(id) +
                " (consecutive failures: " +
                std::to_string(failedData.consecutive_read_failures) +
                ", state: " + connectivityLabel(failedData) + ")");
        }
    }
}

void ModbusScanner::Run(int interval_seconds) {
    // Access the global running flag
    extern std::atomic<bool> running;
    
    while (running.load()) {
        // // Clear screen
        // int ret = system("clear");
        // (void)ret; // Suppress unused variable warning
        const auto now = std::chrono::system_clock::now();
        const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm localTm {};
        localtime_r(&nowTime, &localTm);
        std::ostringstream timestamp;
        timestamp << "\nTimestamp [" << endpointName_ << "]: "
                  << std::put_time(&localTm, "%Y-%m-%d %H:%M:%S");
        logInfo(timestamp.str());
        
        Discover();
        printDevicesData();
        
        logInfo("Next update for endpoint '" + endpointName_ + "' in " +
                std::to_string(interval_seconds) + " seconds...");
        
        // Sleep with periodic checks for shutdown
        for (int i = 0; i < interval_seconds && running.load(); ++i) {
            sleep(1);
        }
    }
    
    logInfo("Modbus scanner thread for endpoint '" + endpointName_ + "' shutting down...");
}
