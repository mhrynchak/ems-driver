#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <map>
#include <curl/curl.h>

#include "../Context.hpp"
#include "DataPoint.hpp"

class SyncService {
public:
    SyncService(std::shared_ptr<Context> ctx, const std::string& dashboardUrl = "https://dashboard-ems-navy.vercel.app");
    ~SyncService();

    // Non-copyable
    SyncService(const SyncService&) = delete;
    SyncService& operator=(const SyncService&) = delete;

    // Start/stop sync operations
    void startPeriodicSync(std::chrono::seconds interval = std::chrono::seconds(30));
    void stop();
    
    // Manual sync operations
    bool syncEnergyFlow();
    bool syncDevices();

private:
    // HTTP client methods
    bool sendHttpPost(const std::string& endpoint, const std::string& jsonData);
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);

    // Data transformation methods
    std::string createEnergyFlowJson(const std::map<uint16_t, DataPoint>& inverterData);
    std::string createDevicesJson(const std::map<uint16_t, DataPoint>& inverterData);

    // Worker thread
    void syncWorkerLoop(std::chrono::seconds interval);

    // Members
    std::shared_ptr<Context> ctx_;
    std::string dashboardUrl_;
    std::string energyFlowEndpoint_;
    std::string devicesEndpoint_;
    
    CURL* curl_;
    std::thread syncThread_;
    std::atomic<bool> running_{false};
};
