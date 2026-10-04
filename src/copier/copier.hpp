// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/copier/copier.hpp
// Purpose:       The copy engine: a scheduler that runs per-ASIAir copy/delete
//                cycles (SMB pull -> incoming/) honouring the copy and delete
//                policies (immediate / batch / after-N-frames / time-of-day).
// Created:       2026-07-22
// Last Modified: 2026-10-04
// Version:       0.1.1
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "config.hpp"
#include "database.hpp"
#include "smb_client.hpp"

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

    // Settle gate: true once this file's (size, mtime) has been seen unchanged
    // for at least cfg.stable_seconds; any change restarts the clock. The ASIAir
    // rewrites each light in place to add its plate solution 15-30 s after
    // capture, so a frame copied on first sight can be torn (see the
    // 2026-10-03 NGC7720 RCA in nightwatcher-ingest). `now` is injectable so
    // tests need no sleeping.
    bool settled(const std::string& air_id, const SmbEntry& e,
                 std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    // Record that a landed copy of this file failed verification: the file must
    // settle again before the next attempt. Returns the failure count so far.
    int verify_failed(const std::string& air_id, const std::string& path);

private:
    void run();
    // Drop settle state for files no longer on the share.
    void prune_seen(const std::string& air_id, const std::vector<SmbEntry>& current);

    struct Seen {
        long long size = 0;
        long long mtime = 0;
        std::chrono::steady_clock::time_point first_seen;
        int failures = 0;
    };

    Config cfg_;
    DbConfig dbcfg_;
    std::thread th_;
    std::atomic<bool> stop_{false};
    std::map<std::string, Seen> seen_;   // "<air id>\n<path>" -> settle state
    std::mutex seen_mtx_;
};

}  // namespace airwatcher
