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
            dc_voltage REAL,
            dc_current REAL,
            dc_power REAL,
            ac_voltage REAL,
            ac_current REAL,
            ac_power REAL,
            error_code INTEGER,
            last_updated INTEGER NOT NULL,  -- When this record was last updated
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        );
        
        CREATE INDEX IF NOT EXISTS idx_last_updated ON inverter_telemetry(last_updated);
        CREATE INDEX IF NOT EXISTS idx_device_name ON inverter_telemetry(device_name);

        CREATE TABLE IF NOT EXISTS device_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            slave_id INTEGER NOT NULL,
            period_start INTEGER NOT NULL,
            period_end INTEGER NOT NULL,
            avg_dc_power REAL NOT NULL DEFAULT 0,
            avg_ac_power REAL NOT NULL,
            sample_count INTEGER NOT NULL,
            daily_yield_kwh REAL NOT NULL,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            UNIQUE(slave_id, period_start)
        );

        CREATE INDEX IF NOT EXISTS idx_device_history_slave_period
            ON device_history(slave_id, period_start);
    )";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    } else {
        std::cout << "Schema initialized successfully\n";
    }

    const char* addAvgDcPowerSql =
        "ALTER TABLE device_history ADD COLUMN avg_dc_power REAL NOT NULL DEFAULT 0";
    rc = sqlite3_exec(db, addAvgDcPowerSql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        const std::string error = errMsg ? errMsg : "";
        if (error.find("duplicate column name") == std::string::npos) {
            std::cerr << "Schema migration error: " << error << std::endl;
        }
        sqlite3_free(errMsg);
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
    sqlite3_bind_double(stmt, 4, static_cast<double>(dataPoint.dc_voltage));
    sqlite3_bind_double(stmt, 5, static_cast<double>(dataPoint.dc_current));
    sqlite3_bind_double(stmt, 6, static_cast<double>(dataPoint.dc_power));
    sqlite3_bind_double(stmt, 7, static_cast<double>(dataPoint.ac_voltage));
    sqlite3_bind_double(stmt, 8, static_cast<double>(dataPoint.ac_current));
    sqlite3_bind_double(stmt, 9, static_cast<double>(dataPoint.ac_power));
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

bool Database::insertDeviceHistory(
    uint16_t slaveId,
    const std::chrono::system_clock::time_point& periodStart,
    const std::chrono::system_clock::time_point& periodEnd,
    double avgDcPower,
    double avgAcPower,
    uint64_t sampleCount,
    double dailyYieldKwh) {

    if (!db) {
        std::cerr << "Database not initialized\n";
        return false;
    }

    const char* sql = R"(
        INSERT OR REPLACE INTO device_history (
            slave_id,
            period_start,
            period_end,
            avg_dc_power,
            avg_ac_power,
            sample_count,
            daily_yield_kwh
        ) VALUES (?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare device_history insert: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    const auto startTs = std::chrono::duration_cast<std::chrono::seconds>(
        periodStart.time_since_epoch()).count();
    const auto endTs = std::chrono::duration_cast<std::chrono::seconds>(
        periodEnd.time_since_epoch()).count();

    sqlite3_bind_int(stmt, 1, slaveId);
    sqlite3_bind_int64(stmt, 2, startTs);
    sqlite3_bind_int64(stmt, 3, endTs);
    sqlite3_bind_double(stmt, 4, avgDcPower);
    sqlite3_bind_double(stmt, 5, avgAcPower);
    sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(sampleCount));
    sqlite3_bind_double(stmt, 7, dailyYieldKwh);

    rc = sqlite3_step(stmt);
    bool success = (rc == SQLITE_DONE);
    if (!success) {
        std::cerr << "Failed to insert device_history row: " << sqlite3_errmsg(db) << std::endl;
    }

    sqlite3_finalize(stmt);
    return success;
}

std::vector<Database::SiteHistoryPoint> Database::getRecentSiteHistory(std::size_t limit) {
    std::vector<SiteHistoryPoint> history;

    if (!db || limit == 0) {
        return history;
    }

    const char* sql = R"(
        WITH per_period AS (
            SELECT
                period_start,
                (period_start / 300) * 300 AS bucket_start,
                SUM(COALESCE(avg_dc_power, 0)) AS total_dc_power,
                SUM(avg_ac_power) AS total_ac_power,
                SUM(daily_yield_kwh) AS total_daily_yield
            FROM device_history
            GROUP BY period_start
        ),
        bucketed AS (
            SELECT
                bucket_start,
                AVG(total_dc_power) AS total_dc_power_avg,
                AVG(total_ac_power) AS total_ac_power_avg,
                MAX(total_daily_yield) AS daily_yield_kwh
            FROM per_period
            GROUP BY bucket_start
            ORDER BY bucket_start DESC
            LIMIT ?
        )
        SELECT
            bucket_start,
            total_dc_power_avg,
            total_ac_power_avg,
            daily_yield_kwh
        FROM bucketed
        ORDER BY bucket_start ASC
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare site history query: " << sqlite3_errmsg(db) << std::endl;
        return history;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(limit));

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const auto bucketStart = sqlite3_column_int64(stmt, 0);
        history.push_back({
            std::chrono::system_clock::time_point{std::chrono::seconds{bucketStart}},
            sqlite3_column_double(stmt, 1),
            sqlite3_column_double(stmt, 2),
            sqlite3_column_double(stmt, 3),
        });
    }

    if (rc != SQLITE_DONE) {
        std::cerr << "Failed to read site history rows: " << sqlite3_errmsg(db) << std::endl;
    }

    sqlite3_finalize(stmt);
    return history;
}
