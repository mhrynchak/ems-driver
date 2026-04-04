#pragma once
#include "IDeviceDriver.hpp"
#include <iostream>

// GenericDriver.hpp
class GenericDriver : public IDeviceDriver {
public:
    std::string getDriverName() const override { return "Generic"; }

    bool readData(modbus_t* mb, int slaveId, DataPoint& data) override {
        modbus_set_slave(mb, slaveId);
        uint16_t regs[7]; // Assuming 7 registers for simplicity, matching ModbusScanner's original read

        // This is a generic read, assuming a common register layout (e.g., starting from 0)
        if (modbus_read_registers(mb, 0, 7, regs) == -1) {
            std::cerr << "GenericDriver: Failed to read registers for slave " << slaveId << ": " << modbus_strerror(errno) << std::endl;
            return false;
        }
        // Populate data point from regs, similar to ModbusScanner's original logic
        data.slave_id = slaveId;
        data.is_active = true;
        data.dc_voltage = regs[0];
        data.dc_current = regs[1];
        data.dc_power = regs[2];
        data.ac_voltage = regs[3];
        data.ac_current = regs[4];
        data.ac_power = regs[5];
        data.error_code = regs[6];
        data.device_name = "Generic Device " + std::to_string(slaveId);
        data.timestamp = std::chrono::system_clock::now();
        return true;
    }
};
