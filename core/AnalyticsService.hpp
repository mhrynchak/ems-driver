#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <map>

class Context;

class AnalyticsService {
public:
    AnalyticsService(std::shared_ptr<Context> ctx);
    ~AnalyticsService();

    AnalyticsService(const AnalyticsService&) = delete;
    AnalyticsService& operator=(const AnalyticsService&) = delete;

    // Aggregation interval in seconds (e.g., 30s for testing, 3600s for production)
    void startPeriodicAggregation(std::chrono::seconds interval = std::chrono::seconds(30));
    void stop();

private:
    void workerLoop(std::chrono::seconds interval);
    void runAggregationCycle(
        const std::chrono::system_clock::time_point& periodEnd,
        std::chrono::seconds interval);
    int64_t dayIndex(const std::chrono::system_clock::time_point& tp) const;

    std::shared_ptr<Context> ctx_;
    std::thread workerThread_;
    std::atomic<bool> running_{false};
    int64_t currentDayIndex_{-1};  // Tracks day boundary for resetting dailyYieldKwh
};