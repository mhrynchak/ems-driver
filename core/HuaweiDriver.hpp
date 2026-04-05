#pragma once
#include "IDeviceDriver.hpp"
#include <iostream>
#include <chrono>
#include <cerrno>
#include <vector>

// HuaweiDriver.hpp
class HuaweiDriver : public IDeviceDriver {
public:
    std::string getDriverName() const override { return "Huawei"; }

    bool readData(modbus_t* mb, int slaveId, DataPoint& data) override {
        modbus_set_slave(mb, slaveId);

        // Huawei Sun2000 register map (integer values with scale factors):
        // 30000: Model(STRING, 15 regs), 32016/32017: PV1 V/A, 32064: InputPower,
        // 32069: Phase A voltage, 32072: Phase A current, 32080: Active power,
        // 32089/32090: Device status/fault.
        uint16_t modelRegs[15];
        uint16_t pvRegs[2];
        uint16_t inputPowerRegs[2];
        uint16_t acVoltageReg;
        uint16_t acCurrentRegs[2];
        uint16_t acPowerRegs[2];
        uint16_t statusReg;
        uint16_t faultReg;
        uint16_t meterStatusReg;
        uint16_t meterPowerRegs[2];
        uint16_t batterySocReg;
        uint16_t batteryPowerRegs[2];

        if (modbus_read_registers(mb, 30000, 15, modelRegs) == -1) {
            std::cerr << "HuaweiDriver: Failed to read model for slave " << slaveId << ": " << modbus_strerror(errno) << std::endl;
            return false;
        }
        if (modbus_read_registers(mb, 32016, 2, pvRegs) == -1) {
            std::cerr << "HuaweiDriver: Failed to read PV registers for slave " << slaveId << ": " << modbus_strerror(errno) << std::endl;
            return false;
        }
        if (modbus_read_registers(mb, 32064, 2, inputPowerRegs) == -1) {
            std::cerr << "HuaweiDriver: Failed to read input power for slave " << slaveId << ": " << modbus_strerror(errno) << std::endl;
            return false;
        }
        if (modbus_read_registers(mb, 32069, 1, &acVoltageReg) == -1) {
            std::cerr << "HuaweiDriver: Failed to read AC voltage for slave " << slaveId << ": " << modbus_strerror(errno) << std::endl;
            return false;
        }
        if (modbus_read_registers(mb, 32072, 2, acCurrentRegs) == -1) {
            std::cerr << "HuaweiDriver: Failed to read AC current for slave " << slaveId << ": " << modbus_strerror(errno) << std::endl;
            return false;
        }
        if (modbus_read_registers(mb, 32080, 2, acPowerRegs) == -1) {
            std::cerr << "HuaweiDriver: Failed to read active power for slave " << slaveId << ": " << modbus_strerror(errno) << std::endl;
            return false;
        }
        if (modbus_read_registers(mb, 32089, 1, &statusReg) == -1) {
            std::cerr << "HuaweiDriver: Failed to read device status for slave " << slaveId << ": " << modbus_strerror(errno) << std::endl;
            return false;
        }
        if (modbus_read_registers(mb, 32090, 1, &faultReg) == -1) {
            std::cerr << "HuaweiDriver: Failed to read fault code for slave " << slaveId << ": " << modbus_strerror(errno) << std::endl;
            return false;
        }

        auto u16_to_i16 = [](uint16_t v) -> int16_t {
            return static_cast<int16_t>(v);
        };
        auto regs_to_i32 = [](uint16_t hi, uint16_t lo) -> int32_t {
            uint32_t raw = (static_cast<uint32_t>(hi) << 16) | static_cast<uint32_t>(lo);
            return static_cast<int32_t>(raw);
        };

        std::vector<uint16_t> modelVec(modelRegs, modelRegs + 15);
        std::string model = regs_to_str(modelVec);

        const float dcVoltage = static_cast<float>(u16_to_i16(pvRegs[0])) / 10.0f;
        const float dcCurrent = static_cast<float>(u16_to_i16(pvRegs[1])) / 100.0f;
        const float dcPower = static_cast<float>(regs_to_i32(inputPowerRegs[0], inputPowerRegs[1]));
        const float acVoltage = static_cast<float>(acVoltageReg) / 10.0f;
        const float acCurrent = static_cast<float>(regs_to_i32(acCurrentRegs[0], acCurrentRegs[1])) / 1000.0f;
        const float acPower = static_cast<float>(regs_to_i32(acPowerRegs[0], acPowerRegs[1]));

        bool hasMeterTelemetry = false;
        float meterActivePower = 0.0f;
        if (modbus_read_registers(mb, 37100, 1, &meterStatusReg) != -1 &&
            modbus_read_registers(mb, 37113, 2, meterPowerRegs) != -1) {
            hasMeterTelemetry = meterStatusReg != 0;
            meterActivePower =
                static_cast<float>(regs_to_i32(meterPowerRegs[0], meterPowerRegs[1]));
        }

        bool hasBatteryTelemetry = false;
        float batterySoc = 0.0f;
        float batteryPower = 0.0f;
        if (modbus_read_registers(mb, 37760, 1, &batterySocReg) != -1 &&
            modbus_read_registers(mb, 37765, 2, batteryPowerRegs) != -1) {
            hasBatteryTelemetry = true;
            batterySoc = static_cast<float>(batterySocReg) / 10.0f;
            batteryPower =
                static_cast<float>(regs_to_i32(batteryPowerRegs[0], batteryPowerRegs[1]));
        }

        data.slave_id = slaveId;
        data.is_active = true;
        data.dc_voltage = dcVoltage;
        data.dc_current = dcCurrent;
        data.dc_power = dcPower;
        data.ac_voltage = acVoltage;
        data.ac_current = acCurrent;
        data.ac_power = acPower;
        data.has_meter_telemetry = hasMeterTelemetry;
        data.meter_active_power = meterActivePower;
        data.has_battery_telemetry = hasBatteryTelemetry;
        data.battery_soc = batterySoc;
        data.battery_power = batteryPower;
        data.error_code = faultReg;
        data.device_name = model.empty()
            ? ("Huawei SUN2000 [" + std::to_string(slaveId) + "]")
            : model;
        data.timestamp = std::chrono::system_clock::now();
        data.driver_name = getDriverName();

        // Consider non on-grid statuses as inactive.
        if (statusReg == 0x0301 || statusReg == 0x0303 || statusReg == 0xA000) {
            data.is_active = false;
        }

        return true;
    }
};
