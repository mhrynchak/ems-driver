#include <iostream>
#include <thread>
#include <csignal>
#include <atomic>

#include "Context.hpp"
#include "core/ModbusScanner.hpp"
#include "core/SyncService.hpp"

// Global flag for graceful shutdown
std::atomic<bool> running{true};

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down gracefully..." << std::endl;
    running = false;
}

int main(int argc, char* argv[]) {
    // Set up signal handler for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    auto ctx = std::make_shared<Context>();
    std::cout << "Initializing database..." << std::endl;
    ctx->db = std::make_shared<Database>("database.db");    
    std::cout << "Initializing cache manager..." << std::endl;
    ctx->cache = std::make_shared<CacheManager>(*(ctx->db));
    std::cout << "Starting cache auto-flush (10 second interval)..." << std::endl;
    ctx->cache->startAutoFlush(std::chrono::seconds(10));

    // Initialize SyncService
    std::cout << "Initializing sync service..." << std::endl;
    ctx->sync = std::make_shared<SyncService>(ctx, "https://dashboard-ems-navy.vercel.app");
    ctx->sync->startPeriodicSync(std::chrono::seconds(10)); // Sync every 10 seconds


    string host = "127.0.0.1";
    int port = 502;
    std::cout << "Connecting to Modbus server at " << host << ":" << port << "..." << std::endl;
    ModbusScanner scanner(ctx, host, port);

    if (!scanner.Connect()) {
        std::cerr << "Failed to connect to Modbus server. Exiting..." << std::endl;
        return -1;
    }

    std::cout << "Starting Modbus scanner thread..." << std::endl;
    std::thread scannerThread([&scanner]() {
        scanner.Run(5); // 5 second scan interval
    });


    
    // Remove the standalone SyncService since it's now part of context
    // SyncService sync;

    std::cout << "System started successfully!" << std::endl;
    std::cout << "Press Ctrl+C to stop the application..." << std::endl;

    // Main thread waits for shutdown signal
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Shutting down..." << std::endl;
    
    // Graceful shutdown - wait for scanner thread to finish
    if (scannerThread.joinable()) {
        scannerThread.join();
    }
    
    // Stop services
    if (ctx->sync) {
        ctx->sync->stop();
    }
    ctx->cache->stop();
    
    std::cout << "Shutdown complete." << std::endl;
    return 0;
}
