#pragma once

#include "Database.hpp"
#include "DataPoint.hpp"

#include <thread>
#include <atomic>
#include <chrono>
#include <string>
#include <map>

using namespace std;

class CacheManager {
public:
    CacheManager(Database& db);
    ~CacheManager();

    void startAutoFlush(std::chrono::seconds interval);
    void stop();

    map<uint16_t, DataPoint> & getInverterCache() { return inverterCache_; }

private:
    void flushToDatabase();
    void workerLoop(std::chrono::seconds interval);

    std::thread flushThread_;
    std::atomic<bool> running_{false};

    Database& db_;
    map<uint16_t, DataPoint> inverterCache_; // Cache mapping slave ID to device name
};
