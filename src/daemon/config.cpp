// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/daemon/config.cpp
// Purpose:       Implementation of the INI-style configuration file parser.
// Created:       2026-07-22
// Last Modified: 2026-07-22
// Version:       0.1.0
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace airwatcher {

namespace {

std::string trim(const std::string& s) {
    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    const auto begin = std::find_if(s.begin(), s.end(), not_space);
    const auto end = std::find_if(s.rbegin(), s.rend(), not_space).base();
    return (begin < end) ? std::string(begin, end) : std::string();
}

bool truthy(const std::string& v) {
    return v == "on" || v == "true" || v == "1" || v == "yes";
}

}  // namespace

Config Config::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("cannot open config file: " + path);
    }

    Config cfg;
    std::string section;
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        const std::string s = trim(line);
        if (s.empty() || s[0] == '#' || s[0] == ';') {
            continue;  // blank line or comment
        }

        if (s.front() == '[' && s.back() == ']') {
            section = trim(s.substr(1, s.size() - 2));
            continue;
        }

        const auto eq = s.find('=');
        if (eq == std::string::npos) {
            throw std::runtime_error("malformed line " + std::to_string(lineno) +
                                     " in " + path + ": '" + s + "'");
        }
        const std::string key = trim(s.substr(0, eq));
        const std::string value = trim(s.substr(eq + 1));

        if (section == "database") {
            if (key == "host") cfg.db_host = value;
            else if (key == "name") cfg.db_name = value;
            else if (key == "user") cfg.db_user = value;
            else if (key == "port") cfg.db_port = std::stoi(value);
        } else if (section == "web") {
            if (key == "bind") cfg.web_bind = value;
            else if (key == "port") cfg.web_port = std::stoi(value);
            else if (key == "tls") cfg.web_tls = truthy(value);
            else if (key == "tls_cert") cfg.web_tls_cert = value;
            else if (key == "tls_key") cfg.web_tls_key = value;
            else if (key == "web_root") cfg.web_root = value;
        } else if (section == "copier") {
            if (key == "incoming") cfg.incoming = value;
            else if (key == "landing") cfg.landing = value;
            else if (key == "poll_interval_s") cfg.poll_interval_s = std::stoi(value);
            else if (key == "stable_seconds") cfg.stable_seconds = std::stoi(value);
            else if (key == "discover_subnet") cfg.discover_subnet = value;
        } else if (section == "extension") {
            if (key == "register") cfg.ext_register = truthy(value);
            else if (key == "name") cfg.ext_name = value;
            else if (key == "label") cfg.ext_label = value;
            else if (key == "heartbeat_seconds") cfg.heartbeat_seconds = std::stoi(value);
        }
        // Unknown sections/keys are ignored for forward compatibility.
    }

    // Default the TLS material to a tls/ dir beside the config file, so enabling
    // HTTPS from the UI has somewhere to auto-generate a self-signed cert.
    if (cfg.web_tls_cert.empty() || cfg.web_tls_key.empty()) {
        const auto slash = path.find_last_of('/');
        const std::string dir = (slash == std::string::npos) ? "." : path.substr(0, slash);
        if (cfg.web_tls_cert.empty()) cfg.web_tls_cert = dir + "/tls/airwatcher-cert.pem";
        if (cfg.web_tls_key.empty()) cfg.web_tls_key = dir + "/tls/airwatcher-key.pem";
    }

    return cfg;
}

}  // namespace airwatcher
