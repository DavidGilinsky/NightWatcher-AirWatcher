// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/db/database.hpp
// Purpose:       RAII wrapper over MariaDB Connector/C (libmariadb) for the
//                AirWatcher tables (asiairs, files, status, log), plus the shared
//                NightWatcher extension-registry and events tables.
// Created:       2026-07-22
// Last Modified: 2026-10-07
// Version:       0.1.2
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace airwatcher {

// Connection parameters. The password is never defaulted in source; supply it
// via the environment (AIRWATCHER_DB_PASSWORD, or NWDB_PASSWORD / NW_DB_PASSWORD).
struct DbConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 3306;
    std::string user = "nightwatcher";
    std::string password;
    std::string database = "nightwatcher";

    static DbConfig from_env();
};

// Full ASIAir record as read from the database.
struct AsiairRow {
    std::string id;          // short id / PK, e.g. "air-backyard"
    std::string name;        // human label
    std::string host;        // ip or resolvable name
    std::string smb_share;   // e.g. "EMMC Images" (empty = auto-detect)
    bool enabled = false;
    // copy policy
    std::string copy_mode = "immediate";  // immediate|batch|after_frames|time_of_day
    int copy_n = 0;                        // batch size / frame threshold
    std::string copy_time;                 // "HH:MM" local (time_of_day)
    // delete policy
    bool delete_after_copy = false;
    std::string delete_mode = "immediate"; // immediate|after_frames|time_of_day
    int delete_n = 0;
    std::string delete_time;               // "HH:MM" local
    std::string delete_via = "none";       // smb|ssh|none
    std::string ssh_user;
    std::string timezone;                  // IANA tz for local-time schedules
    std::string notes;
    std::string created_at;
};

// Editable ASIAir fields. Only members that are set are written, so the same
// struct drives both creation (add) and partial edits (set).
struct AsiairFields {
    std::optional<std::string> name;
    std::optional<std::string> host;
    std::optional<std::string> smb_share;
    std::optional<bool>        enabled;
    std::optional<std::string> copy_mode;
    std::optional<int>         copy_n;
    std::optional<std::string> copy_time;
    std::optional<bool>        delete_after_copy;
    std::optional<std::string> delete_mode;
    std::optional<int>         delete_n;
    std::optional<std::string> delete_time;
    std::optional<std::string> delete_via;
    std::optional<std::string> ssh_user;
    std::optional<std::string> timezone;
    std::optional<std::string> notes;
};

// A file seen on an ASIAir share.
struct RemoteFile {
    std::string path;        // path relative to the share root
    long long   size = 0;
};

class Database {
public:
    explicit Database(const DbConfig& cfg);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Create every AirWatcher table (+ the shared extensions table) if absent.
    void ensure_schema();

    // --- shared extension registry (writes the NightWatcher `extensions` table) ---
    void register_extension(const std::string& name, const std::string& label,
                            const std::string& version, const std::string& data_table);
    void heartbeat(const std::string& name);
    void deregister(const std::string& name);
    // --- shared operational event log ---
    void log_event(const std::string& source, const std::string& level,
                   const std::string& event, const std::string& detail,
                   const std::string& device_id = "");

    // --- asiairs CRUD ---
    void upsert_asiair(const std::string& id, const AsiairFields& f);
    bool update_asiair(const std::string& id, const AsiairFields& f);  // false if absent
    bool remove_asiair(const std::string& id);
    std::vector<AsiairRow> asiairs();
    std::vector<AsiairRow> active_asiairs();  // enabled = 1
    std::optional<AsiairRow> find_asiair(const std::string& id);

    // --- per-file copy/delete state (airwatcher_files) ---
    void mark_seen(const std::string& asiair_id, const std::string& path, long long size);
    // Stored first_seen ("YYYY-MM-DD HH:MM:SS", UTC) of one file, or "" if unknown.
    std::string first_seen_utc(const std::string& asiair_id, const std::string& path);
    std::vector<std::string> copied_paths(const std::string& asiair_id);         // copied_at set
    void mark_copied(const std::string& asiair_id, const std::string& path);
    std::vector<std::string> copied_not_deleted(const std::string& asiair_id);   // for the delete policy
    void mark_deleted(const std::string& asiair_id, const std::string& path);

    // --- status snapshot (airwatcher_status; the registered data_table) ---
    void upsert_status(const std::string& asiair, const std::string& ip, bool enabled,
                       long long total_autorun, long long remaining,
                       const std::string& state, bool touch_last_copy);
    void clear_status(const std::string& asiair);  // when an ASIAir is removed

    // --- action log for AirWatcher's own UI (airwatcher_log) ---
    void log_action(const std::string& asiair, const std::string& action,
                    const std::string& file, const std::string& status,
                    const std::string& detail);
    // Recent log rows as (column-ordered) string cells, newest first:
    // ts_utc, asiair, action, file, status, detail.
    std::vector<std::vector<std::string>> recent_log(int limit);

    // Status snapshot rows for the web UI, ordered by asiair:
    // asiair, ip, enabled, total_autorun, remaining, state, last_copy_utc, updated_at.
    std::vector<std::vector<std::string>> status_rows();

private:
    std::string esc(const std::string& s);
    void exec(const std::string& sql);
    std::vector<std::vector<std::string>> query(const std::string& sql);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace airwatcher
