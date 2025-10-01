#pragma once

#include <vector>
#include <string>
#include <iostream>
#include <modbus/modbus.h>
#include <unistd.h>
#include <iomanip>

using namespace std;

struct InverterData {
    int slave_id;
    bool is_active;
    uint16_t dc_voltage;
    uint16_t dc_current;
    uint16_t dc_power;
    uint16_t ac_voltage;
    uint16_t ac_current;
    uint16_t ac_power;
    uint16_t error_code;
    string device_name;
};

class ModbusScanner {
public:
    ModbusScanner(const string& host = "127.0.0.1", int port = 502, int maxSlavesNum = 100)
        : host(host), port(port), maxSlavesNum(maxSlavesNum), mb(nullptr) {}

    ~ModbusScanner() {
        if (mb) {
            modbus_close(mb);
            modbus_free(mb);
        }
    }

    bool Connect() {
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

    void printDevicesData() {
        if (devices.empty()) {
            cout << "No inverter data available." << endl;
            return;
        }

        cout << "\n=== INVERTER DISCOVERY AND DATA COLLECTION ===" << endl;
        cout << "Found " << devices.size() << " active inverters:" << endl;
        cout << string(80, '=') << endl;

        for (const auto& inverter : devices) {
            cout << "\nInverter: " << inverter.device_name << " (Slave ID: " << inverter.slave_id << ")" << endl;
            cout << string(50, '-') << endl;
            cout << "DC Side:" << endl;
            cout << "  Voltage: " << setw(5) << inverter.dc_voltage << " V" << endl;
            cout << "  Current: " << setw(5) << inverter.dc_current << " A" << endl;
            cout << "  Power:   " << setw(5) << inverter.dc_power << " W" << endl;
            cout << "AC Side:" << endl;
            cout << "  Voltage: " << setw(5) << inverter.ac_voltage << " V" << endl;
            cout << "  Current: " << setw(5) << inverter.ac_current << " A" << endl;
            cout << "  Power:   " << setw(5) << inverter.ac_power << " W" << endl;
            cout << "Status:" << endl;
            cout << "  Error:   " << (inverter.error_code ? "ERROR" : "OK") 
                 << " (" << inverter.error_code << ")" << endl;
        }

        // Summary table
        cout << "\n=== SUMMARY TABLE ===" << endl;
        cout << "Slave | DC V | DC A | DC W | AC V | AC A | AC W | Status" << endl;
        cout << "------|------|------|------|------|------|------|--------" << endl;
        
        int total_dc_power = 0, total_ac_power = 0;
        for (const auto& inv : devices) {
            cout << setw(5) << inv.slave_id << " |"
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

    bool readInverterData(int id, InverterData& data) {
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
        
        return true;
    }

    vector<int> scanForSlaves() {
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

    void Discover() {
        devices.clear();

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
            InverterData data;
            if (readInverterData(id, data)) {
                devices.push_back(data);
                cout << "Successfully read data from slave " << id << endl;
            } else {
                cout << "Failed to read data from slave " << id << endl; 
            }
        }
    }

    void Run(int interval_seconds = 5) {
        cout << "\n=== CONTINUOUS MONITORING MODE ===" << endl;
        cout << "Press Ctrl+C to stop..." << endl;
        
        while (true) {
            system("clear");  // Clear screen
            cout << "=== PV INVERTER MONITORING SYSTEM ===" << endl;
            cout << "Timestamp: " << time(nullptr) << endl;
            
            Discover();
            printDevicesData();
            
            cout << "\nNext update in " << interval_seconds << " seconds..." << endl;
            sleep(interval_seconds);
        }
    }

private:
    string host;
    int port;
    modbus_t *mb;
    int maxSlavesNum;
    bool firstRun = true;
    vector<int> _activeSlaves;
    vector<InverterData> devices;
};
