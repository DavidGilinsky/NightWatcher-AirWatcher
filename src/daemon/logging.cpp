// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/daemon/logging.cpp
// Purpose:       Implementation of thread-safe, UTC-timestamped stderr logging.
// Created:       2026-07-22
// Last Modified: 2026-07-22
// Version:       0.1.0
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#include "logging.hpp"

#include <ctime>
#include <iostream>
#include <mutex>

namespace airwatcher {

namespace {

std::mutex g_log_mtx;

const char* level_str(LogLevel l) {
    switch (l) {
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

}  // namespace

void log_msg(LogLevel level, const std::string& msg) {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    char ts[32];
    std::strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", &tm);

    std::lock_guard<std::mutex> lock(g_log_mtx);
    std::cerr << ts << " [" << level_str(level) << "] " << msg << "\n";
}

}  // namespace airwatcher
