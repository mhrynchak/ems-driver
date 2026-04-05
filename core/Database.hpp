#pragma once

#include <cstddef>
#include <sqlite3.h>
#include <string>
#include <vector>
#include <chrono>

struct sqlite3;
class DataPoint; // Forward declaration

class Database {
public:
    struct SiteHistoryPoint {
        std::chrono::system_clock::time_point bucketStart;
        double totalDcPowerAvg = 0.0;
        double totalAcPowerAvg = 0.0;
        double dailyYieldKwh = 0.0;
    };

    Database(const std::string& dbPath);
    ~Database();

    // Non-copyable
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Movable
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    // Insert operations
    bool insertDataPoint(uint16_t slaveId, const DataPoint& dataPoint);
    bool insertDeviceHistory(
        uint16_t slaveId,
        const std::chrono::system_clock::time_point& periodStart,
        const std::chrono::system_clock::time_point& periodEnd,
        double avgDcPower,
        double avgAcPower,
        uint64_t sampleCount,
        double dailyYieldKwh);
    std::vector<SiteHistoryPoint> getRecentSiteHistory(std::size_t limit);

private:

    void initSchema();

    sqlite3* db{nullptr};
    std::string dbPath;
};
