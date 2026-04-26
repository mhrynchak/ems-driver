#include <iostream>
#include <thread>
#include <csignal>
#include <atomic>
#include <string>
#include <memory>
#include <vector>

#include "Config.hpp"
#include "Context.hpp"
#include "core/ModbusScanner.hpp"
#include "core/SunSpecDriver.hpp"
#include "core/HuaweiDriver.hpp"
#include "core/GenericDriver.hpp"
#include "core/SyncService.hpp"
#include "core/AnalyticsService.hpp"

// Global flag for graceful shutdown
std::atomic<bool> running{true};

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down gracefully..." << std::endl;
    running = false;
}

// Initialize database, cache, and background services
std::shared_ptr<Context> initializeServices(const RuntimeConfig& config) {
    auto ctx = std::make_shared<Context>();
    
    ctx->db = std::make_shared<Database>(config.databasePath);
    ctx->cache = std::make_shared<CacheManager>(*(ctx->db));
    ctx->cache->startAutoFlush(std::chrono::seconds(config.cacheFlushIntervalSeconds));

    ctx->analytics = std::make_shared<AnalyticsService>(ctx);
    ctx->analytics->startPeriodicAggregation(std::chrono::seconds(config.analyticsIntervalSeconds));

    ctx->sync = std::make_shared<SyncService>(
        ctx,
        config.dashboardUrl,
        config.plantId,
        config.plantName,
        config.staleTelemetrySeconds,
        config.importTariffPerKwh,
        config.co2KgPerKwh);
    ctx->sync->startPeriodicSync(std::chrono::seconds(config.syncIntervalSeconds));

    return ctx;
}

// Create and connect Modbus scanners for all configured endpoints
std::vector<std::unique_ptr<ModbusScanner>> initializeScanners(
    const std::shared_ptr<Context>& ctx,
    const RuntimeConfig& config) {
    
    std::vector<std::unique_ptr<ModbusScanner>> scanners;
    scanners.reserve(config.modbusEndpoints.size());

    int endpointIndex = 0;
    for (const auto& endpoint : config.modbusEndpoints) {
        std::cout << "Connecting to Modbus endpoint '" << endpoint.name << "' at "
                  << endpoint.host << ":" << endpoint.port << "..." << std::endl;

        const int cacheKeyOffset = endpointIndex * 1000;
        auto scanner = std::make_unique<ModbusScanner>(
            ctx,
            endpoint.host,
            endpoint.port,
            endpoint.scanStartSlaveId,
            endpoint.scanEndSlaveId,
            endpoint.slaveIds,
            endpoint.name,
            cacheKeyOffset,
            config.offlineFailureThreshold);
        
        scanner->registerDriver("SunSpec", std::make_shared<SunSpecDriver>());
        scanner->registerDriver("Huawei", std::make_shared<HuaweiDriver>());
        scanner->registerDriver("Generic", std::make_shared<GenericDriver>());

        if (!scanner->Connect()) {
            std::cerr << "Failed to connect endpoint '" << endpoint.name << "' ("
                      << endpoint.host << ":" << endpoint.port << "), skipping" << std::endl;
            continue;
        }

        scanners.push_back(std::move(scanner));
        endpointIndex++;
    }

    return scanners;
}

// Start scanner threads for each endpoint
std::vector<std::thread> startScannerThreads(
    std::vector<std::unique_ptr<ModbusScanner>>& scanners,
    const RuntimeConfig& config) {
    
    std::vector<std::thread> scannerThreads;
    scannerThreads.reserve(scanners.size());

    for (auto& scanner : scanners) {
        scannerThreads.emplace_back([scannerPtr = scanner.get(), &config]() {
            scannerPtr->Run(config.scanIntervalSeconds);
        });
    }

    return scannerThreads;
}

// Graceful shutdown of all services and threads
void shutdownGracefully(
    std::vector<std::thread>& scannerThreads,
    const std::shared_ptr<Context>& ctx) {
    
    std::cout << "Shutting down..." << std::endl;
    
    // Wait for scanner threads to finish
    for (auto& scannerThread : scannerThreads) {
        if (scannerThread.joinable()) {
            scannerThread.join();
        }
    }
    
    // Stop background services
    if (ctx->sync) {
        ctx->sync->stop();
    }
    if (ctx->analytics) {
        ctx->analytics->stop();
    }
    ctx->cache->stop();
    
    std::cout << "Shutdown complete." << std::endl;
}

int main(int argc, char* argv[]) {
    // Set up signal handler for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    const std::string configPath = (argc > 1) ? argv[1] : "config.json";
    const RuntimeConfig config = loadRuntimeConfig(configPath);

    // Initialize services
    auto ctx = initializeServices(config);

    // Initialize and connect scanners
    auto scanners = initializeScanners(ctx, config);
    if (scanners.empty()) {
        std::cerr << "No Modbus endpoints connected. Exiting..." << std::endl;
        return -1;
    }

    std::cout << "Starting Modbus scanner thread for " << scanners.size()
              << " endpoint(s)..." << std::endl;

    // Start scanner threads
    auto scannerThreads = startScannerThreads(scanners, config);

    std::cout << "System started successfully!" << std::endl;
    std::cout << "Press Ctrl+C to stop the application..." << std::endl;

    // Main thread waits for shutdown signal
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    shutdownGracefully(scannerThreads, ctx);
    return 0;
}
