#include "ModbusScanner.hpp"
#include "../Context.hpp"
#include <chrono>

ModbusScanner::ModbusScanner(std::shared_ptr<Context> ctx, const string& host, int port, int maxSlavesNum)
    : ctx_(std::move(ctx)), host(host), port(port), maxSlavesNum(maxSlavesNum), mb(nullptr) {}

ModbusScanner::~ModbusScanner() {
    if (mb) {
        modbus_close(mb);
        modbus_free(mb);
    }
}

bool ModbusScanner::Connect() {
    mb = modbus_new_tcp(host.c_str(), port);
    if (mb == nullptr) {
        cerr << "Failed to create Modbus context" << endl;
        return false;
    }

    modbus_set_response_timeout(mb, 0, 50000);  // 50ms timeout (0 sec, 50000 microsec)

    if (modbus_connect(mb) == -1) {
        cerr << "Failed to connect to " << host << ":" << port 
             << " - " << modbus_strerror(errno) << endl;
        modbus_free(mb);
        mb = nullptr;
        return false;
    }

    cout << "Connected to Modbus server at " << host << ":" << port << endl;
    return true;
}

void ModbusScanner::printDevicesData() {
    const auto & devices = ctx_->cache->getInverterCache();
    if (devices.empty()) {
        cout << "No inverter data available." << endl;
        return;
    }

    cout << "\n=== INVERTER DISCOVERY AND DATA COLLECTION ===" << endl;
    cout << "Found " << devices.size() << " active inverters:" << endl;
    cout << string(80, '=') << endl;

    for (const auto& [id, inv] : devices) {
        cout << "\nInverter: " << inv.device_name << " (Slave ID: " << id << ")" << endl;
        cout << string(50, '-') << endl;
        cout << "DC Side:" << endl;
        cout << "  Voltage: " << setw(5) << inv.dc_voltage << " V" << endl;
        cout << "  Current: " << setw(5) << inv.dc_current << " A" << endl;
        cout << "  Power:   " << setw(5) << inv.dc_power << " W" << endl;
        cout << "AC Side:" << endl;
        cout << "  Voltage: " << setw(5) << inv.ac_voltage << " V" << endl;
        cout << "  Current: " << setw(5) << inv.ac_current << " A" << endl;
        cout << "  Power:   " << setw(5) << inv.ac_power << " W" << endl;
        cout << "Status:" << endl;
        cout << "  Error:   " << (inv.error_code ? "ERROR" : "OK") 
             << " (" << inv.error_code << ")" << endl;
    }

    // Summary table
    cout << "\n=== SUMMARY TABLE ===" << endl;
    cout << "Slave | DC V | DC A | DC W | AC V | AC A | AC W | Status" << endl;
    cout << "------|------|------|------|------|------|------|--------" << endl;
    
    int total_dc_power = 0, total_ac_power = 0;
    for (const auto& [id, inv] : devices) {
        cout << setw(5) << id << " |"
             << setw(5) << inv.dc_voltage << " |"
             << setw(5) << inv.dc_current << " |"
             << setw(5) << inv.dc_power << " |"
             << setw(5) << inv.ac_voltage << " |"
             << setw(5) << inv.ac_current << " |"
             << setw(5) << inv.ac_power << " |"
             << setw(7) << (inv.error_code ? "ERROR" : "OK") << endl;
        
        total_dc_power += inv.dc_power;
        total_ac_power += inv.ac_power;
    }
    
    cout << string(60, '-') << endl;
    cout << "Total DC Power: " << total_dc_power << " W" << endl;
    cout << "Total AC Power: " << total_ac_power << " W" << endl;
    cout << "System Efficiency: " << fixed << setprecision(1) 
         << (total_dc_power > 0 ? (double)total_ac_power / total_dc_power * 100 : 0) 
         << "%" << endl;
}

bool ModbusScanner::readInverterData(int id, DataPoint& data) {
    if (!mb) return false;

    modbus_set_slave(mb, id);
    uint16_t registers[7];

    int rc = modbus_read_registers(mb, 0, 7, registers);

    if (rc == -1) {
        cerr << "Failed to read registers: " << modbus_strerror(errno) << endl;
        return false;
    }

    data.slave_id = id;
    data.is_active = true;
    data.dc_voltage = registers[0];
    data.dc_current = registers[1];
    data.dc_power = registers[2];
    data.ac_voltage = registers[3];
    data.ac_current = registers[4];
    data.ac_power = registers[5];
    data.error_code = registers[6];
    data.device_name = "PV Inverter " + to_string(id);
    data.timestamp = std::chrono::system_clock::now();
    
    return true;
}

vector<int> ModbusScanner::scanForSlaves() {
    vector<int> activeSlaves = {};
    if (!mb) {
        cerr << "Not connected to Modbus server" << endl;
        return activeSlaves;
    }

    cout << "Scanning for active Modbus slaves ... " << endl;

    for (int id = 0; id < maxSlavesNum; ++id) {
        modbus_set_slave(mb, id);

        uint16_t test_reg;
        int rc = modbus_read_registers(mb, 0, 1, &test_reg);

        if (rc != -1) {
            activeSlaves.push_back(id);
            cout << "Found active device: " << id << endl;
        }

        // Small delay to avoid overwhelming the network
        // usleep(10000); // 10ms delay
        
        // Progress indicator
        if (id % 10 == 0) {
            cout << "Scanned " << id << "/" << maxSlavesNum << " slaves\r" << flush;
        }
    }

    cout << endl << "Scan complete. Found " << activeSlaves.size() << " active slaves." << endl;
    return activeSlaves;
}

void ModbusScanner::Discover() {
    auto& inverterCache = ctx_->cache->getInverterCache();
    // Don't clear the cache! We need to preserve sync status
    // inverterCache.clear();

    if (firstRun) {
        cout << "\n=== INVERTER SCAN ===" << endl;
        _activeSlaves = scanForSlaves();
        if (_activeSlaves.empty()) {
            cout << "No active slaves found. Trying default slave ID 0..." << endl;
            _activeSlaves.push_back(0);  // Try default slave ID
        }
    }

    firstRun = false;

    for (int id : _activeSlaves) {
        DataPoint newData;
        if (readInverterData(id, newData)) {
            // Check if this is new data or if it has changed
            auto it = inverterCache.find(id);
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
                    oldData.error_code != newData.error_code ||
                    oldData.is_active != newData.is_active
                );
            }
            
            // Only mark as unsynced if data is new or changed
            if (isNewData || dataChanged) {
                newData.synced = false;  // Needs to be flushed
                inverterCache[id] = newData;
                cout << "Updated data for slave " << id << (dataChanged ? " (changed)" : " (new)") << endl;
            } else {
                // Data hasn't changed, preserve sync status and update timestamp
                newData.synced = it->second.synced;  // Keep existing sync status
                inverterCache[id] = newData;
                cout << "Data unchanged for slave " << id << endl;
            }
        } else {
            cout << "Failed to read data from slave " << id << endl; 
        }
    }
}

void ModbusScanner::Run(int interval_seconds) {
    cout << "\n=== CONTINUOUS MONITORING MODE ===" << endl;
    cout << "Press Ctrl+C to stop..." << endl;
    
    // Access the global running flag
    extern std::atomic<bool> running;
    
    while (running.load()) {
        // // Clear screen
        // int ret = system("clear");
        // (void)ret; // Suppress unused variable warning
        cout << "=== PV INVERTER MONITORING SYSTEM ===" << endl;
        cout << "Timestamp: " << time(nullptr) << endl;
        
        Discover();
        printDevicesData();
        
        cout << "\nNext update in " << interval_seconds << " seconds..." << endl;
        
        // Sleep with periodic checks for shutdown
        for (int i = 0; i < interval_seconds && running.load(); ++i) {
            sleep(1);
        }
    }
    
    cout << "\nModbus scanner thread shutting down..." << endl;
}