#include "SyncService.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>

SyncService::SyncService(std::shared_ptr<Context> ctx, const std::string& dashboardUrl)
    : ctx_(std::move(ctx)), dashboardUrl_(dashboardUrl) {
    
    // Set up endpoints
    energyFlowEndpoint_ = dashboardUrl_ + "/api/energy-flow";
    devicesEndpoint_ = dashboardUrl_ + "/api/devices";
    
    // Initialize curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_ = curl_easy_init();
    
    if (!curl_) {
        std::cerr << "Failed to initialize CURL" << std::endl;
    } else {
        std::cout << "SyncService initialized - Dashboard URL: " << dashboardUrl_ << std::endl;
    }
}

SyncService::~SyncService() {
    stop();
    
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
    curl_global_cleanup();
}

void SyncService::startPeriodicSync(std::chrono::seconds interval) {
    if (running_) return;
    
    running_ = true;
    syncThread_ = std::thread(&SyncService::syncWorkerLoop, this, interval);
    std::cout << "Started periodic sync with " << interval.count() << " second interval" << std::endl;
}

void SyncService::stop() {
    if (!running_) return;
    
    running_ = false;
    if (syncThread_.joinable()) {
        syncThread_.join();
    }
    std::cout << "SyncService stopped" << std::endl;
}

void SyncService::syncWorkerLoop(std::chrono::seconds interval) {
    while (running_) {
        // Sync energy flow data
        if (!syncEnergyFlow()) {
            std::cerr << "Failed to sync energy flow data" << std::endl;
        }
        
        // Sync devices data
        if (!syncDevices()) {
            std::cerr << "Failed to sync devices data" << std::endl;
        }
        
        // Sleep with periodic checks for shutdown
        for (int i = 0; i < interval.count() && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

bool SyncService::syncEnergyFlow() {
    if (!ctx_ || !ctx_->cache) {
        std::cerr << "Context or cache not available" << std::endl;
        return false;
    }
    
    const auto& inverterCache = ctx_->cache->getInverterCache();
    if (inverterCache.empty()) {
        std::cout << "No inverter data to sync" << std::endl;
        return true; // Not an error, just no data
    }
    
    std::string jsonData = createEnergyFlowJson(inverterCache);
    return sendHttpPost(energyFlowEndpoint_, jsonData);
}

bool SyncService::syncDevices() {
    if (!ctx_ || !ctx_->cache) {
        std::cerr << "Context or cache not available" << std::endl;
        return false;
    }
    
    const auto& inverterCache = ctx_->cache->getInverterCache();
    if (inverterCache.empty()) {
        std::cout << "No device data to sync" << std::endl;
        return true; // Not an error, just no data
    }
    
    std::string jsonData = createDevicesJson(inverterCache);
    return sendHttpPost(devicesEndpoint_, jsonData);
}

bool SyncService::sendHttpPost(const std::string& endpoint, const std::string& jsonData) {
    if (!curl_) {
        std::cerr << "CURL not initialized" << std::endl;
        return false;
    }
    
    struct curl_slist* headers = nullptr;
    std::string responseData;
    
    // Set headers
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    // Configure CURL
    curl_easy_setopt(curl_, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &responseData);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L); // 10 second timeout
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl_);
    long responseCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &responseCode);
    
    // Cleanup
    curl_slist_free_all(headers);
    
    if (res != CURLE_OK) {
        std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
        return false;
    }
    
    if (responseCode >= 200 && responseCode < 300) {
        std::cout << "Successfully sent data to " << endpoint << " (HTTP " << responseCode << ")" << std::endl;
        return true;
    } else {
        std::cerr << "HTTP error " << responseCode << " for " << endpoint << std::endl;
        std::cerr << "Response: " << responseData << std::endl;
        return false;
    }
}

size_t SyncService::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

std::string SyncService::createEnergyFlowJson(const std::map<uint16_t, DataPoint>& inverterData) {
    // Find the most recent data point (could be improved to aggregate all inverters)
    DataPoint latestData;
    bool hasData = false;
    
    for (const auto& [slaveId, dataPoint] : inverterData) {
        if (!hasData || dataPoint.timestamp > latestData.timestamp) {
            latestData = dataPoint;
            hasData = true;
        }
    }
    
    if (!hasData) {
        return "{}";
    }
    
    // Convert timestamp to ISO 8601 format
    auto timeT = std::chrono::system_clock::to_time_t(latestData.timestamp);
    std::stringstream timestampStream;
    timestampStream << std::put_time(std::gmtime(&timeT), "%Y-%m-%dT%H:%M:%SZ");
    
    // Create JSON in the format expected by the dashboard
    std::stringstream json;
    json << "{\n";
    json << "  \"recordedAt\": \"" << timestampStream.str() << "\",\n";
    json << "  \"dc\": {\n";
    json << "    \"voltage\": " << latestData.dc_voltage / 10.0 << ",\n"; // Convert to proper scale if needed
    json << "    \"current\": " << latestData.dc_current / 10.0 << ",\n";
    json << "    \"power\": " << latestData.dc_power << "\n";
    json << "  },\n";
    json << "  \"ac\": {\n";
    json << "    \"voltage\": " << latestData.ac_voltage / 10.0 << ",\n";
    json << "    \"current\": " << latestData.ac_current / 10.0 << ",\n";
    json << "    \"power\": " << latestData.ac_power << "\n";
    json << "  },\n";
    json << "  \"errorCode\": " << (latestData.error_code != 0 ? std::to_string(latestData.error_code) : "null") << "\n";
    json << "}";
    
    return json.str();
}

std::string SyncService::createDevicesJson(const std::map<uint16_t, DataPoint>& inverterData) {
    std::stringstream json;
    json << "[\n";
    
    bool first = true;
    for (const auto& [slaveId, dataPoint] : inverterData) {
        if (!first) json << ",\n";
        first = false;
        
        // Convert timestamp to ISO 8601 format
        auto timeT = std::chrono::system_clock::to_time_t(dataPoint.timestamp);
        std::stringstream timestampStream;
        timestampStream << std::put_time(std::gmtime(&timeT), "%Y-%m-%dT%H:%M:%SZ");

        // TEMPORARY, SHOULD BE FLOAT IN JSON, BUT DASHBOARD CURRENTLY EXPECTS INTEGER - CONVERTING FOR COMPATIBILITY
        const auto power = static_cast<long long>(std::llround(dataPoint.ac_power));
        const auto capacity = static_cast<long long>(std::llround(dataPoint.ac_power * 2));
        
        json << "  {\n";
        json << "    \"id\": \"inverter-" << slaveId << "\",\n";
        json << "    \"name\": \"" << dataPoint.device_name << "\",\n";
        json << "    \"type\": \"inverter\",\n";
        json << "    \"status\": \"" << (dataPoint.is_active ? "online" : "offline") << "\",\n";
        json << "    \"model\": \"Modbus Inverter\",\n";
        json << "    \"serialNumber\": \"INV-" << std::setfill('0') << std::setw(3) << slaveId << "-UA\",\n";
        json << "    \"lastUpdate\": \"" << timestampStream.str() << "\",\n";
        json << "    \"power\": " << power << ",\n";
        json << "    \"capacity\": " << capacity << "\n"; // Estimate capacity as 2x current power
        json << "  }";
    }
    
    json << "\n]";
    return json.str();
}