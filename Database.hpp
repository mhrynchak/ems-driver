#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <chrono>

struct sqlite3;

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


private:

    void initSchema();

    sqlite3* db{nullptr};
    std::string dbPath;
};