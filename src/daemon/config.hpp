// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/daemon/config.hpp
// Purpose:       Declarations for AirWatcher's INI-style daemon configuration
//                (nwdb connection, own web UI, copier, extension registration).
// Created:       2026-07-22
// Last Modified: 2026-10-04
// Version:       0.1.1
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#pragma once

#include <string>

namespace airwatcher {

// Parsed daemon configuration (simple INI-style file). Individual ASIAirs are
// NOT configured here -- they live in the database and are managed from the web
// UI; this file only configures the daemon itself.
//
// Recognised sections:
//   [database]    host / name / user / port     (nwdb; password comes from env)
//   [web]         bind / port / tls / tls_cert / tls_key   (AirWatcher's own UI)
//   [copier]      incoming / poll_interval_s / stable_seconds / discover_subnet
//   [extension]   register / name / label / heartbeat_seconds
struct Config {
    // --- nwdb (shared NightWatcher database) ---
    std::string db_host = "127.0.0.1";
    std::string db_name = "nightwatcher";
    std::string db_user = "nightwatcher";
    int db_port = 3306;

    // --- AirWatcher's own web UI (distinct from NightWatcher2's :8080) ---
    std::string web_bind = "0.0.0.0";
    int web_port = 8686;
    bool web_tls = false;
    std::string web_tls_cert;   // PEM cert path (default: <config dir>/tls/...)
    std::string web_tls_key;    // PEM key path  (default: <config dir>/tls/...)
    std::string web_root = "/usr/local/airwatcher/web";  // static UI directory

    // --- copier ---
    std::string incoming = "/astronomy/astro-imaging/incoming";  // ingest landing root
    std::string landing = "asiair";  // subdir of `incoming` frames are copied into ("" = incoming itself)
    int poll_interval_s = 60;     // per-ASIAir scan/copy cadence
    int stable_seconds = 60;      // copy only after size+mtime held still this long
    std::string discover_subnet;  // default CIDR for the discovery scan (optional)

    // --- extension registration (read-only tab in the NightWatcher2 web UI) ---
    bool ext_register = true;
    std::string ext_name = "airwatcher";
    std::string ext_label = "AirWatcher";
    int heartbeat_seconds = 30;

    // Load and parse the file at `path`.
    // Throws std::runtime_error on I/O failure or a malformed line.
    static Config load(const std::string& path);
};

}  // namespace airwatcher
