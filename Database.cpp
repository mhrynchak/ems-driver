#include "Database.hpp"
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

void Database::initSchema() {
    std::string sql = R"(
        CREATE TABLE IF NOT EXISTS telemetry (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            ts INTEGER NOT NULL,
            dev_id TEXT NOT NULL,
            power_kw REAL,
            voltage_v REAL,
            current_a REAL,
            status TEXT,
            alarms TEXT,
            synced INTEGER DEFAULT 0
        );
    )";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
    } else {
        std::cout << "Table created successfully\n";
    }
}
