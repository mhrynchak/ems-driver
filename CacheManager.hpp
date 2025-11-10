#pragma once

#include "Database.hpp"
#include "DataPoint.hpp"
#include <string>
#include <map>

using namespace std;

class CacheManager {
public:
    CacheManager() = default;
private:
    Database db;
    map<int, DataPoint> inverterCache; // Cache mapping slave ID to device name
};