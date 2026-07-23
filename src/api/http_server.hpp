// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/api/http_server.hpp
// Purpose:       AirWatcher's own embedded web server (cpp-httplib): serves the
//                static UI and a JSON API for managing/discovering ASIAirs and
//                viewing status/log. Runs on its own port, distinct from
//                NightWatcher2's.
// Created:       2026-07-22
// Last Modified: 2026-07-22
// Version:       0.1.0
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#pragma once

#include <memory>

#include "config.hpp"
#include "database.hpp"

namespace airwatcher {

class HttpServer {
public:
    HttpServer(Config cfg, DbConfig dbcfg);
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void start();  // begin listening on its own thread
    void stop();   // stop listening + join

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace airwatcher
