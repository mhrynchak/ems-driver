#pragma once
#include "IDeviceDriver.hpp"
#include "Utils.hpp"
#include <algorithm>
#include <iostream>
#include <chrono>

// SunSpecDriver.cpp
class SunSpecDriver : public IDeviceDriver {
public:
    std::string getDriverName() const override { return "SunSpec"; }

    bool readData(modbus_t* mb, int slaveId, DataPoint& data) override {
        modbus_set_slave(mb, slaveId);
        
        // SunSpec Model 103 (Common Inverter Model) headers start at 40001
        // and extend through 40131 (ExtVnd4).
        const int START_REG = 40001;
        const int NUM_REGS = 131;
        const int MAX_REGS_PER_CALL = 125;  // Modbus spec limit per Read Holding Registers
        uint16_t regs[NUM_REGS];

        // Read the block of registers for SunSpec Model 103
        int offset = 0;
        while (offset < NUM_REGS) {
            int chunk = std::min(MAX_REGS_PER_CALL, NUM_REGS - offset);
            if (modbus_read_registers(mb, START_REG + offset, chunk, regs + offset) == -1) {
                log_error(
                    __FILE__,
                    __LINE__,
                    "Failed to read registers for slave " + std::to_string(slaveId) +
                    " starting at " + std::to_string(START_REG + offset) +
                    " (count=" + std::to_string(chunk) + ") : " +
                    modbus_strerror(errno));
                return false;
            }
            offset += chunk;
        }

        data.slave_id = slaveId;
        data.is_active = true;
        std::string manufacturer = regs_to_str(std::vector<uint16_t>(regs+4, regs+20)); // 40005-40020
        data.device_name = manufacturer + " SunSpec Device [" + std::to_string(slaveId) + "]";
        data.timestamp = std::chrono::system_clock::now();

        // Parse AC data (referencing mock_inverter_sim_frns.py and SunSpec Model 103)
        // A (Total AC Current) - Modbus addresses 40072, 40073
        data.ac_current = regs_to_float(regs[71], regs[72]);
        // PhVphA (Phase A AC Voltage) - Modbus addresses 40086, 40087
        data.ac_voltage = regs_to_float(regs[85], regs[86]);
        // W (Total AC Power) - Modbus addresses 40092, 40093
        data.ac_power = regs_to_float(regs[91], regs[92]);

        // Parse DC data
        // DCA (DC Current) - Modbus addresses 40104, 40105
        data.dc_current = regs_to_float(regs[103], regs[104]);
        // DCV (DC Voltage) - Modbus addresses 40106, 40107
        data.dc_voltage = regs_to_float(regs[105], regs[106]);
        // DCW (DC Power) - Modbus addresses 40108, 40109
        data.dc_power = regs_to_float(regs[107], regs[108]);

        // Parse Error Code from SunSpec Events
        // Evt1 - Modbus addresses 40120, 40121
        uint32_t event1 = ((uint32_t)regs[119] << 16) | regs[120];
        // Evt2 - Modbus addresses 40122, 40123
        uint32_t event2 = ((uint32_t)regs[121] << 16) | regs[122];
        // Set error_code to 1 if any event is active, 0 otherwise
        data.error_code = (event1 != 0 || event2 != 0) ? 1 : 0;

        return true;
    }
};
