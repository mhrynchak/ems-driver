#pragma once

#include "Database.hpp"
#include "DataPoint.hpp"

#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <map>
#include <mutex>
#include <cstdint>

using namespace std;

class CacheManager {
public:
    CacheManager(Database& db);
    ~CacheManager();

    void startAutoFlush(std::chrono::seconds interval);
    void stop();

    // Thread-safe method to record power samples and update DataPoint accumulators.
    void recordPowerSample(uint16_t slaveId, float dcPower, float acPower);

    // Thread-safe method to iterate and process all DataPoints in the cache
    // The provided function is called with mutex held, so updates are atomic
    template<typename Func>
    void processDataPoints(Func processor) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        for (auto& [slaveId, dataPoint] : inverterCache_) {
            processor(slaveId, dataPoint);
        }
    }

    map<uint16_t, DataPoint> & getInverterCache() { return inverterCache_; }

private:
    void flushToDatabase();
    void workerLoop(std::chrono::seconds interval);

    std::thread flushThread_;
    std::atomic<bool> running_{false};
    std::mutex cacheMutex_;  // Protects access to inverterCache_ during updates

    Database& db_;
    map<uint16_t, DataPoint> inverterCache_; // Cache mapping slave ID to device data and accumulators
};
