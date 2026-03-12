#pragma once
#include <string>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cstdio>

namespace utils {

inline int computeAgeMinutes(const std::string& iso_time) {
    if (iso_time.empty()) return -1;

    std::tm tm = {};
    std::istringstream ss(iso_time);

    
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) {
        ss.clear();
        ss.str(iso_time);
        
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    }
    if (ss.fail()) return -1;

    
    time_t parsed = _mkgmtime(&tm);
    if (parsed == -1) return -1;

    auto parsed_tp = std::chrono::system_clock::from_time_t(parsed);
    auto now = std::chrono::system_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::minutes>(now - parsed_tp);
    return static_cast<int>(age.count());
}

inline std::string currentUtcTimeString() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm gm;
    gmtime_s(&gm, &t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02dZ",
                  gm.tm_year + 1900, gm.tm_mon + 1, gm.tm_mday,
                  gm.tm_hour, gm.tm_min, gm.tm_sec);
    return std::string(buf);
}

inline std::string currentUtcHourString() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm gm;
    gmtime_s(&gm, &t);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:00", gm.tm_hour);
    return std::string(buf);
}

inline std::string currentUtcDateString() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm gm;
    gmtime_s(&gm, &t);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                  gm.tm_year + 1900, gm.tm_mon + 1, gm.tm_mday);
    return std::string(buf);
}

inline std::string formatFloat(float val, int precision = 1) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, val);
    return std::string(buf);
}

} 
