#include "CacheManager.hpp"
#include <iostream>

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
