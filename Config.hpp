#pragma once

#include <chrono>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct ModbusEndpointConfig {
    std::string name = "default";
    std::string host = "127.0.0.1";
    int port = 502;
    int scanStartSlaveId = 1;
    int scanEndSlaveId = 247;
    std::vector<int> slaveIds;
};

struct RuntimeConfig {
    std::string databasePath = "database.db";
    std::string plantId = "default-plant";
    std::string plantName = "Modbus Plant";
    std::string modbusHost = "127.0.0.1";
    int modbusPort = 502;
    int modbusScanStartSlaveId = 1;
    int modbusScanEndSlaveId = 247;
    std::vector<int> modbusSlaveIds;
    std::vector<ModbusEndpointConfig> modbusEndpoints;
    int scanIntervalSeconds = 5;
    int cacheFlushIntervalSeconds = 10;
    int analyticsIntervalSeconds = 30;
    int syncIntervalSeconds = 10;
    int offlineFailureThreshold = 3;
    int staleTelemetrySeconds = 60;
    double importTariffPerKwh = 4.32;
    double co2KgPerKwh = 0.38;
    std::string dashboardUrl = "http://localhost:3000";
};

inline RuntimeConfig loadRuntimeConfig(const std::string& configPath) {
    RuntimeConfig config;
    std::ifstream input(configPath);
    if (!input.is_open()) {
        std::cout << "Config file not found at '" << configPath << "', using built-in defaults" << std::endl;
        return config;
    }

    try {
        const auto json = nlohmann::json::parse(input);
        auto parseSlaveIds = [](const nlohmann::json& node) {
            std::vector<int> slaveIds;
            std::set<int> seen;

            for (const auto& value : node) {
                const int slaveId = value.get<int>();
                if (slaveId < 1 || slaveId > 247) {
                    throw std::runtime_error("slaveIds values must be in range 1..247");
                }
                if (seen.insert(slaveId).second) {
                    slaveIds.push_back(slaveId);
                }
            }

            return slaveIds;
        };
        auto parseSlaveScanRange = [](const nlohmann::json& node, int& startId, int& endId) {
            if (node.contains("start")) {
                startId = node["start"].get<int>();
            }
            if (node.contains("end")) {
                endId = node["end"].get<int>();
            }
            if (startId < 1 || endId > 247 || startId > endId) {
                throw std::runtime_error("slaveScanRange must stay within 1..247 and have start <= end");
            }
        };

        if (json.contains("database") && json["database"].is_object()) {
            const auto& database = json["database"];
            if (database.contains("path")) {
                config.databasePath = database["path"].get<std::string>();
            }
        }

        if (json.contains("plant") && json["plant"].is_object()) {
            const auto& plant = json["plant"];
            if (plant.contains("id")) {
                config.plantId = plant["id"].get<std::string>();
            }
            if (plant.contains("name")) {
                config.plantName = plant["name"].get<std::string>();
            }
        }

        if (json.contains("modbus") && json["modbus"].is_object()) {
            const auto& modbus = json["modbus"];
            if (modbus.contains("host")) {
                config.modbusHost = modbus["host"].get<std::string>();
            }
            if (modbus.contains("port")) {
                config.modbusPort = modbus["port"].get<int>();
            }
            if (modbus.contains("scanIntervalSeconds")) {
                config.scanIntervalSeconds = modbus["scanIntervalSeconds"].get<int>();
            }
            if (modbus.contains("slaveScanRange") && modbus["slaveScanRange"].is_object()) {
                parseSlaveScanRange(
                    modbus["slaveScanRange"],
                    config.modbusScanStartSlaveId,
                    config.modbusScanEndSlaveId);
            }
            if (modbus.contains("slaveIds") && modbus["slaveIds"].is_array()) {
                config.modbusSlaveIds = parseSlaveIds(modbus["slaveIds"]);
            }

            if (modbus.contains("endpoints") && modbus["endpoints"].is_array()) {
                std::vector<ModbusEndpointConfig> endpoints;
                for (const auto& endpoint : modbus["endpoints"]) {
                    if (!endpoint.is_object()) {
                        continue;
                    }

                    ModbusEndpointConfig parsed;
                    parsed.scanStartSlaveId = config.modbusScanStartSlaveId;
                    parsed.scanEndSlaveId = config.modbusScanEndSlaveId;
                    parsed.slaveIds = config.modbusSlaveIds;
                    if (endpoint.contains("name")) {
                        parsed.name = endpoint["name"].get<std::string>();
                    }
                    if (endpoint.contains("host")) {
                        parsed.host = endpoint["host"].get<std::string>();
                    }
                    if (endpoint.contains("port")) {
                        parsed.port = endpoint["port"].get<int>();
                    }
                    if (endpoint.contains("slaveScanRange") && endpoint["slaveScanRange"].is_object()) {
                        parseSlaveScanRange(
                            endpoint["slaveScanRange"],
                            parsed.scanStartSlaveId,
                            parsed.scanEndSlaveId);
                    }
                    if (endpoint.contains("slaveIds") && endpoint["slaveIds"].is_array()) {
                        parsed.slaveIds = parseSlaveIds(endpoint["slaveIds"]);
                    }
                    endpoints.push_back(parsed);
                }

                if (!endpoints.empty()) {
                    config.modbusEndpoints = endpoints;
                }
            }
        }

        // Backward compatibility: if no endpoints array is provided, use host/port.
        if (config.modbusEndpoints.empty()) {
            ModbusEndpointConfig endpoint;
            endpoint.name = "default";
            endpoint.host = config.modbusHost;
            endpoint.port = config.modbusPort;
            endpoint.scanStartSlaveId = config.modbusScanStartSlaveId;
            endpoint.scanEndSlaveId = config.modbusScanEndSlaveId;
            endpoint.slaveIds = config.modbusSlaveIds;
            config.modbusEndpoints.push_back(endpoint);
        }

        if (json.contains("services") && json["services"].is_object()) {
            const auto& services = json["services"];
            if (services.contains("cacheFlushIntervalSeconds")) {
                config.cacheFlushIntervalSeconds = services["cacheFlushIntervalSeconds"].get<int>();
            }
            if (services.contains("analyticsIntervalSeconds")) {
                config.analyticsIntervalSeconds = services["analyticsIntervalSeconds"].get<int>();
            }
            if (services.contains("syncIntervalSeconds")) {
                config.syncIntervalSeconds = services["syncIntervalSeconds"].get<int>();
            }
            if (services.contains("dashboardUrl")) {
                config.dashboardUrl = services["dashboardUrl"].get<std::string>();
            }
        }

        if (json.contains("health") && json["health"].is_object()) {
            const auto& health = json["health"];
            if (health.contains("offlineFailureThreshold")) {
                config.offlineFailureThreshold = health["offlineFailureThreshold"].get<int>();
            }
            if (health.contains("staleTelemetrySeconds")) {
                config.staleTelemetrySeconds = health["staleTelemetrySeconds"].get<int>();
            }
        }

        if (json.contains("analytics") && json["analytics"].is_object()) {
            const auto& analytics = json["analytics"];
            if (analytics.contains("importTariffPerKwh")) {
                config.importTariffPerKwh = analytics["importTariffPerKwh"].get<double>();
            }
            if (analytics.contains("co2KgPerKwh")) {
                config.co2KgPerKwh = analytics["co2KgPerKwh"].get<double>();
            }
        }

        std::cout << "Loaded config from '" << configPath << "'" << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "Failed to parse config '" << configPath << "': " << ex.what()
                  << ". Using built-in defaults" << std::endl;
    }

    return config;
}
