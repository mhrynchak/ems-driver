#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <chrono>

struct sqlite3;
class DataPoint; // Forward declaration

class Database {
public:
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

private:

    void initSchema();

    sqlite3* db{nullptr};
    std::string dbPath;
};