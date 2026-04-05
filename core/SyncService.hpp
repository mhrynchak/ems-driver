#pragma once

#include <atomic>
#include <chrono>
#include <curl/curl.h>
#include <map>
#include <memory>
#include <string>
#include <thread>

#include "../Context.hpp"
#include "DataPoint.hpp"

class SyncService {
public:
    SyncService(
        std::shared_ptr<Context> ctx,
        const std::string& dashboardUrl = "http://localhost:3000",
        const std::string& plantId = "default-plant",
        const std::string& plantName = "Modbus Plant",
        int staleTelemetrySeconds = 60,
        double importTariffPerKwh = 4.32,
        double co2KgPerKwh = 0.38);
    ~SyncService();

    SyncService(const SyncService&) = delete;
    SyncService& operator=(const SyncService&) = delete;

    void startPeriodicSync(std::chrono::seconds interval = std::chrono::seconds(30));
    void stop();

    bool syncSiteOverview();
    bool syncDevices();
    bool syncSiteHistory();
    bool syncAlerts();

private:
    bool sendHttpPost(const std::string& endpoint, const std::string& jsonData);
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);

    std::string createSiteOverviewJson(const std::map<uint16_t, DataPoint>& inverterData);
    std::string createDevicesJson(const std::map<uint16_t, DataPoint>& inverterData);
    std::string createSiteHistoryJson(const std::map<uint16_t, DataPoint>& inverterData);
    std::string createAlertsJson(const std::map<uint16_t, DataPoint>& inverterData);

    void syncWorkerLoop(std::chrono::seconds interval);

    std::shared_ptr<Context> ctx_;
    std::string dashboardUrl_;
    std::string plantId_;
    std::string plantName_;
    std::string siteOverviewEndpoint_;
    std::string devicesEndpoint_;
    std::string siteHistoryEndpoint_;
    std::string alertsEndpoint_;
    std::chrono::seconds staleTelemetryThreshold_;
    double importTariffPerKwh_;
    double co2KgPerKwh_;
    std::map<std::string, std::string> previousOpenAlerts_;

    CURL* curl_;
    std::thread syncThread_;
    std::atomic<bool> running_{false};
};
