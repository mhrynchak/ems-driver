#pragma once

#include <memory>

#include "core/Database.hpp"
#include "core/CacheManager.hpp"

// Forward declaration to avoid circular dependency
class SyncService;
class AnalyticsService;

class Context: public std::enable_shared_from_this<Context> {
public:
    std::shared_ptr<Database> db;
    std::shared_ptr<CacheManager> cache;
    std::shared_ptr<SyncService> sync;
    std::shared_ptr<AnalyticsService> analytics;
};
