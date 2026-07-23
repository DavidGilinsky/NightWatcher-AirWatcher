// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/copier/copier.cpp
// Purpose:       Copy-engine implementation: per-ASIAir SMB walk -> incoming/ copy
//                with copy/delete policy selection, plus a scheduler thread that
//                re-reads the active ASIAirs each poll so enable/disable is live.
// Created:       2026-07-22
// Last Modified: 2026-07-23
// Version:       0.1.0
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#include "copier.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <set>
#include <thread>

#include "logging.hpp"
#include "smb_client.hpp"

namespace fs = std::filesystem;

namespace airwatcher {

namespace {

// Image roots on the ASIAir share to scan for FITS.
const std::vector<std::string> kImageRoots = {"Autorun", "Plan"};
const std::vector<std::string> kFitsSuffixes = {".fit", ".fits"};

// True if the host's local wall-clock is within [hhmm, hhmm+window) today.
bool at_time_of_day(const std::string& hhmm, int window_s) {
    if (hhmm.size() < 4) return false;
    const int th = std::atoi(hhmm.substr(0, 2).c_str());
    const int tm = std::atoi(hhmm.substr(hhmm.find(':') == std::string::npos ? 2 : 3, 2).c_str());
    const std::time_t now = std::time(nullptr);
    std::tm lt{};
    localtime_r(&now, &lt);
    const int now_s = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
    const int tgt_s = th * 3600 + tm * 60;
    return now_s >= tgt_s && now_s < tgt_s + window_s;
}

// Which uncopied files to copy this cycle, per the ASIAir's copy policy.
std::vector<SmbEntry> select_copy(const AsiairRow& air, const std::vector<SmbEntry>& uncopied,
                                  int poll_s) {
    if (uncopied.empty()) return {};
    const int window = std::max(120, poll_s + 5);
    if (air.copy_mode == "batch") {
        const size_t n = air.copy_n > 0 ? static_cast<size_t>(air.copy_n) : uncopied.size();
        return {uncopied.begin(), uncopied.begin() + std::min(n, uncopied.size())};
    }
    if (air.copy_mode == "after_frames") {
        return (air.copy_n > 0 && uncopied.size() >= static_cast<size_t>(air.copy_n))
                   ? uncopied : std::vector<SmbEntry>{};
    }
    if (air.copy_mode == "time_of_day") {
        return at_time_of_day(air.copy_time, window) ? uncopied : std::vector<SmbEntry>{};
    }
    return uncopied;  // "immediate" (default)
}

// Which copied-not-deleted files to delete this cycle, per the delete policy.
std::vector<std::string> select_delete(const AsiairRow& air, const std::vector<std::string>& cand,
                                       int poll_s) {
    if (cand.empty()) return {};
    const int window = std::max(120, poll_s + 5);
    if (air.delete_mode == "after_frames") {
        return (air.delete_n > 0 && cand.size() >= static_cast<size_t>(air.delete_n))
                   ? cand : std::vector<std::string>{};
    }
    if (air.delete_mode == "time_of_day") {
        return at_time_of_day(air.delete_time, window) ? cand : std::vector<std::string>{};
    }
    return cand;  // "immediate" (default)
}

}  // namespace

Copier::Copier(Config cfg, DbConfig dbcfg) : cfg_(std::move(cfg)), dbcfg_(std::move(dbcfg)) {}

Copier::~Copier() { stop(); }

CycleResult Copier::run_once(Database& db, const AsiairRow& air) {
    CycleResult res;
    const std::string share = air.smb_share.empty() ? "EMMC Images" : air.smb_share;

    std::unique_ptr<SmbClient> smb;
    try {
        smb = std::make_unique<SmbClient>(air.host, share);
    } catch (const std::exception& e) {
        res.state = "error";
        res.detail = e.what();
        db.upsert_status(air.id, air.host, air.enabled, 0, 0, res.state, false);
        db.log_action(air.id, "scan", "", "error", e.what());
        return res;
    }
    if (!smb->reachable()) {
        res.state = "unreachable";
        db.upsert_status(air.id, air.host, air.enabled, 0, 0, res.state, false);
        return res;
    }

    // Walk the image roots for FITS files.
    std::vector<SmbEntry> current;
    for (const auto& root : kImageRoots) {
        auto part = smb->walk(root, kFitsSuffixes);
        current.insert(current.end(), std::make_move_iterator(part.begin()),
                       std::make_move_iterator(part.end()));
    }
    for (const auto& f : current) db.mark_seen(air.id, f.path, f.size);

    const auto copied_vec = db.copied_paths(air.id);
    const std::set<std::string> copied_set(copied_vec.begin(), copied_vec.end());
    std::vector<SmbEntry> uncopied;
    for (const auto& f : current) {
        if (copied_set.find(f.path) == copied_set.end()) uncopied.push_back(f);
    }
    res.total = static_cast<long long>(current.size());
    res.remaining = static_cast<long long>(uncopied.size());

    // --- copy policy ---
    const auto to_copy = select_copy(air, uncopied, cfg_.poll_interval_s);

    // Publish the current scan immediately (and periodically in the loop below) so
    // the status isn't stale during a long cycle -- a big first sync copies many
    // files in a single immediate cycle, and we want live total/remaining meanwhile.
    db.upsert_status(air.id, air.host, air.enabled, res.total, res.remaining,
                     to_copy.empty() ? (res.remaining > 0 ? "pending" : "idle") : "copying", false);

    for (const auto& f : to_copy) {
        if (stop_.load()) break;
        // Land frames in <incoming>/<landing>/ (the "asiair" landing zone that
        // nightwatcher-ingest watches), preserving the ASIAir's own path below it.
        fs::path dest = fs::path(cfg_.incoming);
        if (!cfg_.landing.empty()) dest /= cfg_.landing;
        dest /= f.path;
        const std::string part = dest.string() + ".part";
        try {
            fs::create_directories(dest.parent_path());
            smb->copy_to(f.path, part);
            fs::rename(part, dest);
            db.mark_copied(air.id, f.path);
            db.log_action(air.id, "copy", f.path, "ok", "");
            ++res.copied;
            if (res.remaining > 0) --res.remaining;
            // Live progress after each file (cheap single-row upsert), so a long
            // first sync shows remaining ticking down instead of sitting stale.
            db.upsert_status(air.id, air.host, air.enabled, res.total, res.remaining,
                             "copying", true);
        } catch (const std::exception& e) {
            std::error_code ec;
            fs::remove(part, ec);
            db.log_action(air.id, "copy", f.path, "error", e.what());
            db.log_event("airwatcher", "error", "copy", air.id + ": " + f.path + ": " + e.what());
        }
    }
    if (res.copied > 0) {
        db.log_event("airwatcher", "info", "copy",
                     air.id + ": copied " + std::to_string(res.copied) + " frame(s)");
    }

    // --- delete policy ---
    if (air.delete_after_copy && air.delete_via != "none") {
        const auto to_del = select_delete(air, db.copied_not_deleted(air.id), cfg_.poll_interval_s);
        for (const auto& p : to_del) {
            if (stop_.load()) break;
            try {
                if (air.delete_via == "smb") {
                    smb->remove(p);
                    db.mark_deleted(air.id, p);
                    db.log_action(air.id, "delete", p, "ok", "");
                    ++res.deleted;
                } else {  // "ssh" is wired in the delete-engine phase
                    db.log_action(air.id, "delete", p, "skip", "delete_via=" + air.delete_via +
                                                                   " not yet implemented");
                }
            } catch (const std::exception& e) {
                db.log_action(air.id, "delete", p, "error", e.what());
                db.log_event("airwatcher", "warning", "delete",
                             air.id + ": " + p + ": " + e.what());
            }
        }
    }

    res.state = res.remaining > 0 ? "pending" : "idle";
    db.upsert_status(air.id, air.host, air.enabled, res.total, res.remaining, res.state,
                     res.copied > 0);
    return res;
}

void Copier::run() {
    try {
        Database db(dbcfg_);
        while (!stop_.load()) {
            std::vector<AsiairRow> active;
            try {
                active = db.active_asiairs();
            } catch (const std::exception& e) {
                log_error(std::string("copier: active_asiairs: ") + e.what());
            }
            for (const auto& air : active) {
                if (stop_.load()) break;
                try {
                    const CycleResult r = run_once(db, air);
                    if (r.copied > 0 || r.deleted > 0) {
                        log_info(air.id + ": " + std::to_string(r.copied) + " copied, " +
                                 std::to_string(r.deleted) + " deleted, " +
                                 std::to_string(r.remaining) + " remaining of " +
                                 std::to_string(r.total));
                    }
                } catch (const std::exception& e) {
                    log_error("copier: cycle " + air.id + ": " + e.what());
                }
            }
            // Interruptible sleep in 1s slices so stop is responsive.
            for (int s = 0; s < cfg_.poll_interval_s && !stop_.load(); ++s) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    } catch (const std::exception& e) {
        log_error(std::string("copier: fatal (db): ") + e.what());
    }
}

void Copier::start() {
    stop_.store(false);
    th_ = std::thread([this] { run(); });
}

void Copier::stop() {
    stop_.store(true);
    if (th_.joinable()) th_.join();
}

}  // namespace airwatcher
