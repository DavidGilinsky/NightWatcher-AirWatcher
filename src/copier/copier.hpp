// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/copier/copier.hpp
// Purpose:       The copy engine: a scheduler that runs per-ASIAir copy/delete
//                cycles (SMB pull -> incoming/) honouring the copy and delete
//                policies (immediate / batch / after-N-frames / time-of-day).
// Created:       2026-07-22
// Last Modified: 2026-07-22
// Version:       0.1.0
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <thread>

#include "config.hpp"
#include "database.hpp"

namespace airwatcher {

// Result of one ASIAir cycle (also surfaced to the web UI / logs).
struct CycleResult {
    long long total = 0;       // FITS files currently in the image roots
    long long remaining = 0;   // not yet copied
    int copied = 0;            // copied this cycle
    int deleted = 0;           // deleted this cycle
    std::string state;         // "idle" | "copying" | "pending" | "unreachable" | "error"
    std::string detail;        // error text, if any
};

class Copier {
public:
    Copier(Config cfg, DbConfig dbcfg);
    ~Copier();

    void start();  // spawn the scheduler thread
    void stop();   // stop + join

    // Run a single copy/delete cycle for one ASIAir against `db`. Public so the
    // CLI and tests can drive a cycle without the scheduler thread. Updates the
    // file-state, status snapshot, and logs; never throws (errors -> CycleResult).
    CycleResult run_once(Database& db, const AsiairRow& air);

private:
    void run();

    Config cfg_;
    DbConfig dbcfg_;
    std::thread th_;
    std::atomic<bool> stop_{false};
};

}  // namespace airwatcher
