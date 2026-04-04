// IDeviceDriver.hpp
#pragma once
#include "DataPoint.hpp"
#include <modbus/modbus.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

class IDeviceDriver {
public:
    virtual ~IDeviceDriver() = default;
    
    virtual std::string getDriverName() const = 0;

    virtual bool readData(modbus_t* mb, int slaveId, DataPoint& data) = 0;

    std::string regs_to_str(const std::vector<uint16_t>& regs) {
        std::string str;
        str.reserve(regs.size() * 2);

        for (uint16_t reg : regs) {
            str.push_back(static_cast<char>((reg >> 8) & 0xFF));   // high byte
            str.push_back(static_cast<char>(reg & 0xFF));          // low byte
        }

        while (!str.empty() && (str.back() == '\0' || str.back() == ' ')) {
            str.pop_back();  // drop padding added by str_to_regs()
        }

        return str;
    }
    // Helper function to convert two 16-bit Modbus registers to a float (big-endian)
    // This reverses the float_to_regs logic from mock_inverter_sim_frns.py
    float regs_to_float(uint16_t reg1, uint16_t reg2) {
        // Combine the two 16-bit registers into the original 32-bit payload
        uint32_t combined_value = (static_cast<uint32_t>(reg1) << 16) | reg2;
        float f;
        std::memcpy(&f, &combined_value, sizeof(float));
        return f;
    }
};
