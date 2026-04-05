#include "SyncService.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace {

using json = nlohmann::json;

std::string trimTrailingSlashes(const std::string& value) {
    if (value.empty()) {
        return value;
    }

    std::string trimmed = value;
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }

    return trimmed;
}

std::string toIsoString(const std::chrono::system_clock::time_point& timestamp) {
    const auto timeT = std::chrono::system_clock::to_time_t(timestamp);
    std::tm utcTm {};
    gmtime_r(&timeT, &utcTm);

    std::ostringstream stream;
    stream << std::put_time(&utcTm, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

std::string makeDeviceId(const DataPoint& dataPoint, uint16_t fallbackSlaveId) {
    const std::string endpoint =
        dataPoint.source_endpoint.empty() ? "default" : dataPoint.source_endpoint;
    const uint16_t sourceSlaveId =
        dataPoint.source_slave_id != 0 ? dataPoint.source_slave_id : fallbackSlaveId;

    return endpoint + "-inverter-" + std::to_string(sourceSlaveId);
}

std::chrono::system_clock::time_point statusTimestamp(const DataPoint& dataPoint) {
    if (dataPoint.last_read_attempt.time_since_epoch().count() != 0) {
        return dataPoint.last_read_attempt;
    }

    return dataPoint.timestamp;
}

bool contributesToSiteTotals(const DataPoint& dataPoint) {
    return dataPoint.has_successful_read &&
           dataPoint.connectivity_state != ConnectivityState::Offline;
}

bool hasVisibleTelemetry(const DataPoint& dataPoint) {
    return dataPoint.has_successful_read &&
           dataPoint.connectivity_state != ConnectivityState::Offline;
}

json telemetryValue(const DataPoint& dataPoint, double value) {
    return hasVisibleTelemetry(dataPoint) ? json(value) : json(nullptr);
}

std::string normalizeStatus(const DataPoint& dataPoint) {
    if (dataPoint.connectivity_state == ConnectivityState::Offline) {
        return "offline";
    }

    if (dataPoint.connectivity_state == ConnectivityState::Connecting ||
        dataPoint.consecutive_read_failures > 0 ||
        dataPoint.error_code != 0 ||
        !dataPoint.is_active) {
        return "warning";
    }

    return "online";
}

std::chrono::system_clock::time_point floorToFiveMinutes(
    const std::chrono::system_clock::time_point& timestamp) {
    const auto secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(
        timestamp.time_since_epoch()).count();
    const auto floored = secondsSinceEpoch - (secondsSinceEpoch % (5 * 60));

    return std::chrono::system_clock::time_point{std::chrono::seconds{floored}};
}

json buildDeferredMetric(const std::string& note) {
    return json{
        {"value", nullptr},
        {"status", "not_connected"},
        {"note", note},
    };
}

json buildAvailableMetric(const std::string& note) {
    return json{
        {"value", nullptr},
        {"status", "available"},
        {"note", note},
    };
}

json buildPlannedMetric(const std::string& note) {
    return json{
        {"value", nullptr},
        {"status", "planned"},
        {"note", note},
    };
}

json buildAlertPayload(
    const std::string& plantId,
    const std::map<uint16_t, DataPoint>& inverterData,
    std::chrono::seconds staleThreshold) {
    json alerts = json::array();
    const auto now = std::chrono::system_clock::now();

    for (const auto& [slaveId, dataPoint] : inverterData) {
        const auto deviceId = makeDeviceId(dataPoint, slaveId);
        const auto referenceTimestamp = statusTimestamp(dataPoint);
        const auto age = now - referenceTimestamp;

        if (dataPoint.connectivity_state == ConnectivityState::Connecting) {
            alerts.push_back({
                {"id", "device-connecting-" + deviceId},
                {"plantId", plantId},
                {"severity", "info"},
                {"state", "open"},
                {"type", "device_connecting"},
                {"message", dataPoint.device_name +
                                " is waiting for a first successful telemetry read."},
                {"deviceId", deviceId},
                {"triggeredAt", toIsoString(referenceTimestamp)},
            });
            continue;
        }

        if (dataPoint.connectivity_state == ConnectivityState::Offline) {
            alerts.push_back({
                {"id", "telemetry-offline-" + deviceId},
                {"plantId", plantId},
                {"severity", "critical"},
                {"state", "open"},
                {"type", "telemetry_offline"},
                {"message", dataPoint.device_name +
                                " is offline after repeated Modbus read failures."},
                {"deviceId", deviceId},
                {"triggeredAt", toIsoString(referenceTimestamp)},
            });
            continue;
        }

        if (dataPoint.error_code != 0) {
            alerts.push_back({
                {"id", "device-fault-" + deviceId},
                {"plantId", plantId},
                {"severity", "warning"},
                {"state", "open"},
                {"type", "device_fault"},
                {"message", dataPoint.device_name + " reports error code " +
                                std::to_string(dataPoint.error_code) + "."},
                {"deviceId", deviceId},
                {"triggeredAt", toIsoString(referenceTimestamp)},
            });
        }

        if (dataPoint.consecutive_read_failures > 0) {
            alerts.push_back({
                {"id", "telemetry-degraded-" + deviceId},
                {"plantId", plantId},
                {"severity", "warning"},
                {"state", "open"},
                {"type", "telemetry_degraded"},
                {"message", dataPoint.device_name +
                                " has intermittent read failures and is being monitored."},
                {"deviceId", deviceId},
                {"triggeredAt", toIsoString(referenceTimestamp)},
            });
        } else if (age > staleThreshold) {
            alerts.push_back({
                {"id", "telemetry-stale-" + deviceId},
                {"plantId", plantId},
                {"severity", "critical"},
                {"state", "open"},
                {"type", "telemetry_stale"},
                {"message", dataPoint.device_name +
                                " has stale telemetry and may be unreachable."},
                {"deviceId", deviceId},
                {"triggeredAt", toIsoString(referenceTimestamp)},
            });
        } else if (!dataPoint.is_active) {
            alerts.push_back({
                {"id", "device-inactive-" + deviceId},
                {"plantId", plantId},
                {"severity", "warning"},
                {"state", "open"},
                {"type", "device_inactive"},
                {"message", dataPoint.device_name +
                                " is reachable but not active according to the driver status."},
                {"deviceId", deviceId},
                {"triggeredAt", toIsoString(referenceTimestamp)},
            });
        }
    }

    return alerts;
}

}  // namespace

SyncService::SyncService(
    std::shared_ptr<Context> ctx,
    const std::string& dashboardUrl,
    const std::string& plantId,
    const std::string& plantName,
    int staleTelemetrySeconds,
    double importTariffPerKwh,
    double co2KgPerKwh)
    : ctx_(std::move(ctx)),
      dashboardUrl_(trimTrailingSlashes(dashboardUrl)),
      plantId_(plantId.empty() ? "default-plant" : plantId),
      plantName_(plantName.empty() ? "Modbus Plant" : plantName),
      staleTelemetryThreshold_(std::chrono::seconds(std::max(1, staleTelemetrySeconds))),
      importTariffPerKwh_(std::max(0.0, importTariffPerKwh)),
      co2KgPerKwh_(std::max(0.0, co2KgPerKwh)),
      curl_(nullptr) {
    siteOverviewEndpoint_ = dashboardUrl_ + "/api/site-overview";
    devicesEndpoint_ = dashboardUrl_ + "/api/devices";
    siteHistoryEndpoint_ = dashboardUrl_ + "/api/site-history";
    alertsEndpoint_ = dashboardUrl_ + "/api/alerts";

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_ = curl_easy_init();

    if (!curl_) {
        std::cerr << "Failed to initialize CURL" << std::endl;
    } else {
        std::cout << "SyncService initialized - Dashboard URL: " << dashboardUrl_
                  << ", Plant ID: " << plantId_ << std::endl;
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
    if (running_) {
        return;
    }

    running_ = true;
    syncThread_ = std::thread(&SyncService::syncWorkerLoop, this, interval);
    std::cout << "Started periodic sync with " << interval.count()
              << " second interval" << std::endl;
}

void SyncService::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (syncThread_.joinable()) {
        syncThread_.join();
    }
    std::cout << "SyncService stopped" << std::endl;
}

void SyncService::syncWorkerLoop(std::chrono::seconds interval) {
    while (running_) {
        if (!syncSiteOverview()) {
            std::cerr << "Failed to sync site overview data" << std::endl;
        }

        if (!syncDevices()) {
            std::cerr << "Failed to sync devices data" << std::endl;
        }

        if (!syncSiteHistory()) {
            std::cerr << "Failed to sync site history data" << std::endl;
        }

        if (!syncAlerts()) {
            std::cerr << "Failed to sync alerts data" << std::endl;
        }

        for (int i = 0; i < interval.count() && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

bool SyncService::syncSiteOverview() {
    if (!ctx_ || !ctx_->cache) {
        std::cerr << "Context or cache not available" << std::endl;
        return false;
    }

    const auto& inverterCache = ctx_->cache->getInverterCache();
    if (inverterCache.empty()) {
        std::cout << "No site overview data to sync" << std::endl;
        return true;
    }

    const std::string jsonData = createSiteOverviewJson(inverterCache);
    return sendHttpPost(siteOverviewEndpoint_, jsonData);
}

bool SyncService::syncDevices() {
    if (!ctx_ || !ctx_->cache) {
        std::cerr << "Context or cache not available" << std::endl;
        return false;
    }

    const auto& inverterCache = ctx_->cache->getInverterCache();
    if (inverterCache.empty()) {
        std::cout << "No device data to sync" << std::endl;
        return true;
    }

    const std::string jsonData = createDevicesJson(inverterCache);
    return sendHttpPost(devicesEndpoint_, jsonData);
}

bool SyncService::syncSiteHistory() {
    if (!ctx_ || !ctx_->cache) {
        std::cerr << "Context or cache not available" << std::endl;
        return false;
    }

    const auto& inverterCache = ctx_->cache->getInverterCache();
    if (inverterCache.empty()) {
        std::cout << "No site history data to sync" << std::endl;
        return true;
    }

    const std::string jsonData = createSiteHistoryJson(inverterCache);
    return sendHttpPost(siteHistoryEndpoint_, jsonData);
}

bool SyncService::syncAlerts() {
    if (!ctx_ || !ctx_->cache) {
        std::cerr << "Context or cache not available" << std::endl;
        return false;
    }

    const auto& inverterCache = ctx_->cache->getInverterCache();
    if (inverterCache.empty()) {
        std::cout << "No alert data to sync" << std::endl;
        return true;
    }

    const std::string jsonData = createAlertsJson(inverterCache);
    if (jsonData == "[]") {
        std::cout << "No active alerts to sync" << std::endl;
        return true;
    }
    return sendHttpPost(alertsEndpoint_, jsonData);
}

bool SyncService::sendHttpPost(const std::string& endpoint, const std::string& jsonData) {
    if (!curl_) {
        std::cerr << "CURL not initialized" << std::endl;
        return false;
    }

    struct curl_slist* headers = nullptr;
    std::string responseData;

    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_reset(curl_);
    curl_easy_setopt(curl_, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, jsonData.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(jsonData.size()));
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &responseData);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);
    curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);

    const CURLcode res = curl_easy_perform(curl_);
    long responseCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &responseCode);

    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
        return false;
    }

    if (responseCode >= 200 && responseCode < 300) {
        std::cout << "Successfully sent data to " << endpoint << " (HTTP "
                  << responseCode << ")" << std::endl;
        return true;
    }

    std::cerr << "HTTP error " << responseCode << " for " << endpoint << std::endl;
    std::cerr << "Response: " << responseData << std::endl;
    return false;
}

size_t SyncService::writeCallback(
    void* contents,
    size_t size,
    size_t nmemb,
    void* userp) {
    const size_t totalSize = size * nmemb;
    auto* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

std::string SyncService::createSiteOverviewJson(
    const std::map<uint16_t, DataPoint>& inverterData) {
    if (inverterData.empty()) {
        return "{}";
    }

    double totalDcPower = 0.0;
    double totalAcPower = 0.0;
    double totalAcCurrent = 0.0;
    double totalDcVoltage = 0.0;
    double totalAcVoltage = 0.0;
    int dcVoltageSamples = 0;
    int acVoltageSamples = 0;
    int activeDevices = 0;
    double dailyYieldKwh = 0.0;
    double meterPower = 0.0;
    int meterSamples = 0;
    double batteryPower = 0.0;
    double batterySoc = 0.0;
    int batterySamples = 0;
    auto latestTimestamp = std::chrono::system_clock::time_point{};

    for (const auto& [slaveId, dataPoint] : inverterData) {
        (void)slaveId;
        latestTimestamp = std::max(latestTimestamp, statusTimestamp(dataPoint));

        if (!contributesToSiteTotals(dataPoint)) {
            continue;
        }

        totalDcPower += dataPoint.dc_power;
        totalAcPower += dataPoint.ac_power;
        totalAcCurrent += dataPoint.ac_current;
        dailyYieldKwh += dataPoint.dailyYieldKwh;

        if (dataPoint.has_meter_telemetry) {
            meterPower += dataPoint.meter_active_power;
            ++meterSamples;
        }

        if (dataPoint.has_battery_telemetry) {
            batteryPower += dataPoint.battery_power;
            batterySoc += dataPoint.battery_soc;
            ++batterySamples;
        }

        if (dataPoint.dc_voltage > 0.0f) {
            totalDcVoltage += dataPoint.dc_voltage;
            ++dcVoltageSamples;
        }

        if (dataPoint.ac_voltage > 0.0f) {
            totalAcVoltage += dataPoint.ac_voltage;
            ++acVoltageSamples;
        }

        if (dataPoint.is_active) {
            ++activeDevices;
        }
    }

    const bool hasGridMeter = meterSamples > 0;
    const bool hasBattery = batterySamples > 0;
    const double gridImportPower = hasGridMeter ? std::max(0.0, -meterPower) : 0.0;
    const double gridExportPower = hasGridMeter ? std::max(0.0, meterPower) : 0.0;
    const double homeLoadPower = hasGridMeter
        ? std::max(0.0, totalAcPower + batteryPower + gridImportPower - gridExportPower)
        : 0.0;
    const double averageBatterySoc =
        hasBattery ? (batterySoc / static_cast<double>(batterySamples)) : 0.0;
    const bool hasTariffConfig = importTariffPerKwh_ > 0.0;
    const bool hasSavingsAnalytics = hasTariffConfig;
    const bool hasCo2Tracking = co2KgPerKwh_ > 0.0;
    const double estimatedMoneySaved =
        hasSavingsAnalytics ? (dailyYieldKwh * importTariffPerKwh_) : 0.0;
    const double estimatedCo2ReducedKg =
        hasCo2Tracking ? (dailyYieldKwh * co2KgPerKwh_) : 0.0;

    const json alerts = buildAlertPayload(plantId_, inverterData, staleTelemetryThreshold_);

    json payload = {
        {"plantId", plantId_},
        {"plantName", plantName_},
        {"recordedAt", toIsoString(latestTimestamp)},
        {"totalDcPower", totalDcPower},
        {"totalAcPower", totalAcPower},
        {"avgDcVoltage", dcVoltageSamples > 0
                             ? json(totalDcVoltage / dcVoltageSamples)
                             : json(nullptr)},
        {"avgAcVoltage", acVoltageSamples > 0
                             ? json(totalAcVoltage / acVoltageSamples)
                             : json(nullptr)},
        {"totalAcCurrent", totalAcCurrent},
        {"activeDevices", activeDevices},
        {"totalDevices", static_cast<int>(inverterData.size())},
        {"alarmCount", static_cast<int>(alerts.size())},
        {"dailyYieldKwh", dailyYieldKwh},
        {"gridImportPower", hasGridMeter ? json(gridImportPower) : json(nullptr)},
        {"gridExportPower", hasGridMeter ? json(gridExportPower) : json(nullptr)},
        {"homeLoadPower", hasGridMeter ? json(homeLoadPower) : json(nullptr)},
        {"batteryPower", hasBattery ? json(batteryPower) : json(nullptr)},
        {"batterySoc", hasBattery ? json(averageBatterySoc) : json(nullptr)},
        {"moneySaved", hasSavingsAnalytics ? json(estimatedMoneySaved) : json(nullptr)},
        {"co2ReducedKg", hasCo2Tracking ? json(estimatedCo2ReducedKg) : json(nullptr)},
        {"efficiency", totalDcPower > 0.0
                           ? json((totalAcPower / totalDcPower) * 100.0)
                           : json(nullptr)},
        {"capabilities",
         {
             {"hasGridMeter", hasGridMeter},
             {"hasBattery", hasBattery},
             {"hasEvCharger", false},
             {"hasTariffConfig", hasTariffConfig},
             {"hasSavingsAnalytics", hasSavingsAnalytics},
             {"hasCo2Tracking", hasCo2Tracking},
         }},
        {"deferred",
         {
             {"savings", hasSavingsAnalytics
                             ? buildAvailableMetric(
                                   "Estimated from today's production and the configured import tariff.")
                             : buildPlannedMetric(
                                   "Set an import tariff in the driver config to enable avoided-cost estimates.")},
             {"co2Reduction", hasCo2Tracking
                                  ? buildAvailableMetric(
                                        "Estimated from daily production and the configured grid carbon factor.")
                                  : buildPlannedMetric(
                                        "Set a CO2 factor in the driver config to enable carbon analytics.")},
             {"gridImportExport", hasGridMeter
                                      ? buildAvailableMetric(
                                            "Live net grid exchange is coming from the meter feed.")
                                      : buildDeferredMetric(
                                            "Needs a grid meter or utility feed in the driver.")},
             {"homeLoad", hasGridMeter
                              ? buildAvailableMetric(
                                    "Derived from plant output, battery power, and current grid exchange.")
                              : buildDeferredMetric(
                                    "Can be derived once plant output and grid telemetry are both present.")},
             {"battery", hasBattery
                             ? buildAvailableMetric(
                                   "Battery state of charge and signed charge/discharge power are live.")
                             : buildDeferredMetric(
                                   "Battery SOC and power are not exposed by the current driver yet.")},
             {"evCharger", buildDeferredMetric(
                               "EV charging telemetry will appear after charger support is added.")},
         }},
    };

    return payload.dump();
}

std::string SyncService::createDevicesJson(
    const std::map<uint16_t, DataPoint>& inverterData) {
    json payload = json::array();

    for (const auto& [slaveId, dataPoint] : inverterData) {
        const std::string endpoint =
            dataPoint.source_endpoint.empty() ? "default" : dataPoint.source_endpoint;
        const uint16_t sourceSlaveId =
            dataPoint.source_slave_id != 0 ? dataPoint.source_slave_id : slaveId;

        payload.push_back({
            {"plantId", plantId_},
            {"deviceId", makeDeviceId(dataPoint, slaveId)},
            {"name", dataPoint.device_name},
            {"sourceEndpoint", endpoint},
            {"sourceSlaveId", sourceSlaveId},
            {"driverType", dataPoint.driver_name.empty() ? "Unknown" : dataPoint.driver_name},
            {"deviceType", "inverter"},
            {"status", normalizeStatus(dataPoint)},
            {"recordedAt", toIsoString(statusTimestamp(dataPoint))},
            {"dcVoltage", telemetryValue(dataPoint, dataPoint.dc_voltage)},
            {"dcCurrent", telemetryValue(dataPoint, dataPoint.dc_current)},
            {"dcPower", telemetryValue(dataPoint, dataPoint.dc_power)},
            {"acVoltage", telemetryValue(dataPoint, dataPoint.ac_voltage)},
            {"acCurrent", telemetryValue(dataPoint, dataPoint.ac_current)},
            {"acPower", telemetryValue(dataPoint, dataPoint.ac_power)},
            {"meterActivePower", dataPoint.has_meter_telemetry
                                     ? telemetryValue(dataPoint, dataPoint.meter_active_power)
                                     : json(nullptr)},
            {"batterySoc", dataPoint.has_battery_telemetry
                               ? telemetryValue(dataPoint, dataPoint.battery_soc)
                               : json(nullptr)},
            {"batteryPower", dataPoint.has_battery_telemetry
                                 ? telemetryValue(dataPoint, dataPoint.battery_power)
                                 : json(nullptr)},
            {"errorCode", hasVisibleTelemetry(dataPoint) && dataPoint.error_code != 0
                              ? json(std::to_string(dataPoint.error_code))
                              : json(nullptr)},
            {"dailyYieldKwh", telemetryValue(dataPoint, dataPoint.dailyYieldKwh)},
            {"lastPeriodAvgPower", telemetryValue(dataPoint, dataPoint.lastHourAvgPower)},
        });
    }

    return payload.dump();
}

std::string SyncService::createSiteHistoryJson(
    const std::map<uint16_t, DataPoint>& inverterData) {
    json payload = json::array();

    if (ctx_ && ctx_->db) {
        const auto history = ctx_->db->getRecentSiteHistory(72);
        for (const auto& point : history) {
            payload.push_back({
                {"plantId", plantId_},
                {"bucketStart", toIsoString(point.bucketStart)},
                {"bucketSize", "5m"},
                {"totalDcPowerAvg", point.totalDcPowerAvg},
                {"totalAcPowerAvg", point.totalAcPowerAvg},
                {"dailyYieldKwh", point.dailyYieldKwh},
            });
        }
    }

    if (!payload.empty()) {
        return payload.dump();
    }

    if (inverterData.empty()) {
        return "[]";
    }

    double totalDcPowerAvg = 0.0;
    double totalAcPowerAvg = 0.0;
    double dailyYieldKwh = 0.0;

    for (const auto& [slaveId, dataPoint] : inverterData) {
        (void)slaveId;
        if (!contributesToSiteTotals(dataPoint)) {
            continue;
        }
        totalDcPowerAvg += dataPoint.dc_power;
        totalAcPowerAvg +=
            dataPoint.lastHourAvgPower > 0.0 ? dataPoint.lastHourAvgPower : dataPoint.ac_power;
        dailyYieldKwh += dataPoint.dailyYieldKwh;
    }

    const auto bucketStart = floorToFiveMinutes(std::chrono::system_clock::now());

    payload.push_back({
        {"plantId", plantId_},
        {"bucketStart", toIsoString(bucketStart)},
        {"bucketSize", "5m"},
        {"totalDcPowerAvg", totalDcPowerAvg},
        {"totalAcPowerAvg", totalAcPowerAvg},
        {"dailyYieldKwh", dailyYieldKwh},
    });

    return payload.dump();
}

std::string SyncService::createAlertsJson(
    const std::map<uint16_t, DataPoint>& inverterData) {
    const json openAlerts =
        buildAlertPayload(plantId_, inverterData, staleTelemetryThreshold_);
    json updates = openAlerts;
    std::map<std::string, std::string> currentOpenAlerts;

    for (const auto& alert : openAlerts) {
        const auto id = alert.value("id", "");
        if (!id.empty()) {
            currentOpenAlerts[id] = alert.dump();
        }
    }

    for (const auto& [id, serializedAlert] : previousOpenAlerts_) {
        if (currentOpenAlerts.find(id) != currentOpenAlerts.end()) {
            continue;
        }

        json resolvedAlert = json::parse(serializedAlert);
        resolvedAlert["state"] = "resolved";
        updates.push_back(resolvedAlert);
    }

    previousOpenAlerts_ = std::move(currentOpenAlerts);
    return updates.dump();
}
