#include "CacheManager.hpp"
#include <iostream>
#include <iomanip>

CacheManager::CacheManager(Database& db) : db_(db) {};

CacheManager::~CacheManager() {
    stop();
    flushToDatabase(); // ensure nothing lost
}

void CacheManager::startAutoFlush(std::chrono::seconds interval) {
    if (running_) return;
    running_ = true;
    flushThread_ = std::thread(&CacheManager::workerLoop, this, interval);
}

void CacheManager::stop() {
    if (!running_) return;
    running_ = false;
    if (flushThread_.joinable()) flushThread_.join();
}

void CacheManager::recordPowerSample(uint16_t slaveId, float dcPower, float acPower) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = inverterCache_.find(slaveId);
    if (it != inverterCache_.end()) {
        it->second.dcPowerSampleSum += static_cast<double>(dcPower);
        it->second.acPowerSampleSum += static_cast<double>(acPower);
        it->second.sampleCount++;
        // std::cout << "  [CacheManager] Device " << slaveId << " recorded samples: dc="
        //             << std::fixed << std::setprecision(1) << dcPower << "W, ac="
        //             << acPower << "W, count="
        //             << it->second.sampleCount << std::endl;
    } else {
        // Device not in cache yet - warn
        // std::cerr << "[CacheManager] WARNING: Tried to record sample for device " << slaveId 
        //             << " but it's not in cache yet. Make sure ModbusScanner has discovered it."
        //             << std::endl;
    }
}

void CacheManager::workerLoop(std::chrono::seconds interval) {
    while (running_) {
        std::this_thread::sleep_for(interval);
        flushToDatabase();
    }
}

void CacheManager::flushToDatabase() {
    if (inverterCache_.empty()) {
        return; // Nothing to flush
    }

    int successCount = 0;
    int totalCount = inverterCache_.size();

    for (auto& [slaveId, dataPoint] : inverterCache_) {
        // Always attempt to update the database with current state
        if (db_.insertDataPoint(slaveId, dataPoint)) {
            // Mark as synced after successful insert/update
            dataPoint.synced = true;
            successCount++;
        }
    }

    std::cout << "Database sync: " << successCount << "/" << totalCount 
              << " devices updated\n";
}
