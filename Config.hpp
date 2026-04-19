#pragma once

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct ModbusEndpointConfig {
    std::string name = "default";
    std::string host = "127.0.0.1";
    int port = 502;
};

struct RuntimeConfig {
    std::string databasePath = "database.db";
    std::string plantId = "default-plant";
    std::string plantName = "Modbus Plant";
    std::string modbusHost = "127.0.0.1";
    int modbusPort = 502;
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

            if (modbus.contains("endpoints") && modbus["endpoints"].is_array()) {
                std::vector<ModbusEndpointConfig> endpoints;
                for (const auto& endpoint : modbus["endpoints"]) {
                    if (!endpoint.is_object()) {
                        continue;
                    }

                    ModbusEndpointConfig parsed;
                    if (endpoint.contains("name")) {
                        parsed.name = endpoint["name"].get<std::string>();
                    }
                    if (endpoint.contains("host")) {
                        parsed.host = endpoint["host"].get<std::string>();
                    }
                    if (endpoint.contains("port")) {
                        parsed.port = endpoint["port"].get<int>();
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
            config.modbusEndpoints.push_back({"default", config.modbusHost, config.modbusPort});
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
