#include "AnalyticsService.hpp"

#include "../Context.hpp"

#include <iostream>
#include <iomanip>

AnalyticsService::AnalyticsService(std::shared_ptr<Context> ctx)
    : ctx_(std::move(ctx)) {}

AnalyticsService::~AnalyticsService() {
    stop();
}

void AnalyticsService::startPeriodicAggregation(std::chrono::seconds interval) {
    if (running_) {
        return;
    }

    running_ = true;
    workerThread_ = std::thread(&AnalyticsService::workerLoop, this, interval);
    std::cout << "AnalyticsService started with " << interval.count()
              << " second interval" << std::endl;
}

void AnalyticsService::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    std::cout << "AnalyticsService stopped" << std::endl;
}

void AnalyticsService::workerLoop(std::chrono::seconds interval) {
    // std::cout << "[AnalyticsService] Aggregation worker thread started (interval: " 
    //           << interval.count() << " second(s))" << std::endl;
    
    while (running_) {
        for (int i = 0; i < interval.count() && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!running_) {
            break;
        }

        runAggregationCycle(std::chrono::system_clock::now(), interval);
    }
    
    // std::cout << "[AnalyticsService] Aggregation worker thread exiting" << std::endl;
}

void AnalyticsService::runAggregationCycle(
    const std::chrono::system_clock::time_point& periodEnd,
    std::chrono::seconds interval) {

    if (!ctx_ || !ctx_->cache || !ctx_->db) {
        std::cerr << "AnalyticsService: context not initialized" << std::endl;
        return;
    }

    // Log the aggregation cycle start with timestamp
    auto now_time = std::chrono::system_clock::to_time_t(periodEnd);
    std::cout << "[AnalyticsService] Starting aggregation cycle at " 
              << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S") << std::endl;
    
    const auto nowDayIndex = dayIndex(periodEnd);
    
    // Reset all devices' dailyYieldKwh when calendar day changes
    if (currentDayIndex_ != nowDayIndex) {
        currentDayIndex_ = nowDayIndex;
        // std::cout << "[AnalyticsService] Day boundary detected - resetting daily yields" << std::endl;
        ctx_->cache->processDataPoints([](uint16_t slaveId, DataPoint& dataPoint) {
            dataPoint.dailyYieldKwh = 0.0;
        });
    }

    const auto periodStart = periodEnd - interval;
    const double intervalHours =
        static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(interval).count()) / 3600.0;

    int insertedCount = 0;
    int skippedCount = 0;

    // Thread-safe iteration over all DataPoints with accumulator fields
    ctx_->cache->processDataPoints([&](uint16_t slaveId, DataPoint& dataPoint) {
        if (dataPoint.sampleCount == 0) {
            skippedCount++;
            return;  // Skip devices with no samples
        }

        const double avgDcPower = dataPoint.dcPowerSampleSum / static_cast<double>(dataPoint.sampleCount);
        const double avgAcPower = dataPoint.acPowerSampleSum / static_cast<double>(dataPoint.sampleCount);

        // Update the DataPoint with the hourly average (for dashboard visibility)
        dataPoint.lastHourAvgPower = avgAcPower;

        // Accumulate energy across the day from power samples
        const double intervalEnergyKwh = (avgAcPower * intervalHours) / 1000.0;
        dataPoint.dailyYieldKwh += intervalEnergyKwh;

        std::cout << "  [Device " << slaveId << "] avg_power=" << avgAcPower << "W, "
                  << "samples=" << dataPoint.sampleCount << ", "
                  << "daily_yield=" << std::fixed << std::setprecision(3) 
                  << dataPoint.dailyYieldKwh << " kWh" << std::endl;

        // Persist to device_history table
        if (ctx_->db->insertDeviceHistory(
                slaveId,
                periodStart,
                periodEnd,
                avgDcPower,
                avgAcPower,
                dataPoint.sampleCount,
                dataPoint.dailyYieldKwh)) {
            insertedCount++;
        }

        // Reset accumulators for next period
        dataPoint.dcPowerSampleSum = 0.0;
        dataPoint.acPowerSampleSum = 0.0;
        dataPoint.sampleCount = 0;
    });

    std::cout << "[AnalyticsService] Cycle complete: " << insertedCount 
              << " rows inserted, " << skippedCount << " devices skipped (no samples)"
              << std::endl << std::endl;
}

int64_t AnalyticsService::dayIndex(const std::chrono::system_clock::time_point& tp) const {
    const auto hoursSinceEpoch =
        std::chrono::duration_cast<std::chrono::hours>(tp.time_since_epoch()).count();
    return hoursSinceEpoch / 24;
}
