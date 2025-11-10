#include "Database.hpp"
#include "DataPoint.hpp"
#include <iostream>

Database::Database(const std::string& dbPath): dbPath(dbPath) {
    int rc;

    rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        db = nullptr;
    } else {
        std::cout << "Opened database successfully\n";
    }

    initSchema();
}

Database::~Database() {
    if (db) {
        sqlite3_close(db);
    }
}

// Move constructor
Database::Database(Database&& other) noexcept 
    : db(other.db), dbPath(std::move(other.dbPath)) {
    other.db = nullptr;
}

// Move assignment operator
Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        if (db) {
            sqlite3_close(db);
        }
        
        // Move from other
        db = other.db;
        dbPath = std::move(other.dbPath);
        
        // Reset other
        other.db = nullptr;
    }
    return *this;
}

void Database::initSchema() {
    std::string sql = R"(
        CREATE TABLE IF NOT EXISTS inverter_telemetry (
            slave_id INTEGER PRIMARY KEY,  -- Use slave_id as primary key since each slave is unique
            device_name TEXT NOT NULL,
            is_active INTEGER NOT NULL,
            dc_voltage INTEGER,
            dc_current INTEGER, 
            dc_power INTEGER,
            ac_voltage INTEGER,
            ac_current INTEGER,
            ac_power INTEGER,
            error_code INTEGER,
            last_updated INTEGER NOT NULL,  -- When this record was last updated
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        );
        
        CREATE INDEX IF NOT EXISTS idx_last_updated ON inverter_telemetry(last_updated);
        CREATE INDEX IF NOT EXISTS idx_device_name ON inverter_telemetry(device_name);
    )";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    } else {
        std::cout << "Schema initialized successfully\n";
    }
}

bool Database::insertDataPoint(uint16_t slaveId, const DataPoint& dataPoint) {
    if (!db) {
        std::cerr << "Database not initialized\n";
        return false;
    }

    const char* sql = R"(
        INSERT OR REPLACE INTO inverter_telemetry (
            slave_id, device_name, is_active,
            dc_voltage, dc_current, dc_power,
            ac_voltage, ac_current, ac_power, 
            error_code, last_updated
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    // Convert timestamp to Unix timestamp (seconds since epoch)
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        dataPoint.timestamp.time_since_epoch()).count();

    // Bind parameters
    sqlite3_bind_int(stmt, 1, slaveId);
    sqlite3_bind_text(stmt, 2, dataPoint.device_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, dataPoint.is_active ? 1 : 0);
    sqlite3_bind_int(stmt, 4, dataPoint.dc_voltage);
    sqlite3_bind_int(stmt, 5, dataPoint.dc_current);
    sqlite3_bind_int(stmt, 6, dataPoint.dc_power);
    sqlite3_bind_int(stmt, 7, dataPoint.ac_voltage);
    sqlite3_bind_int(stmt, 8, dataPoint.ac_current);
    sqlite3_bind_int(stmt, 9, dataPoint.ac_power);
    sqlite3_bind_int(stmt, 10, dataPoint.error_code);
    sqlite3_bind_int64(stmt, 11, timestamp);

    // Execute the statement
    rc = sqlite3_step(stmt);
    bool success = (rc == SQLITE_DONE);
    
    if (!success) {
        std::cerr << "Failed to insert/update data: " << sqlite3_errmsg(db) << std::endl;
    }

    sqlite3_finalize(stmt);
    return success;
}
