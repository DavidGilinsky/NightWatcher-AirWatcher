// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/api/http_server.cpp
// Purpose:       Implementation of AirWatcher's embedded web server + JSON API
//                (cpp-httplib + nlohmann/json). Optional Bearer-token auth and
//                self-signed TLS; serves the static UI from web_root.
// Created:       2026-07-22
// Last Modified: 2026-07-22
// Version:       0.1.0
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#include "http_server.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "airwatcher/version.hpp"
#include "discovery.hpp"
#include "logging.hpp"
#include "smb_client.hpp"
#include "tls_cert.hpp"

using json = nlohmann::json;

namespace airwatcher {

namespace {

json to_json(const AsiairRow& a) {
    return json{
        {"id", a.id}, {"name", a.name}, {"host", a.host}, {"smb_share", a.smb_share},
        {"enabled", a.enabled}, {"copy_mode", a.copy_mode}, {"copy_n", a.copy_n},
        {"copy_time", a.copy_time}, {"delete_after_copy", a.delete_after_copy},
        {"delete_mode", a.delete_mode}, {"delete_n", a.delete_n}, {"delete_time", a.delete_time},
        {"delete_via", a.delete_via}, {"ssh_user", a.ssh_user}, {"timezone", a.timezone},
        {"notes", a.notes}, {"created_at", a.created_at}};
}

AsiairFields fields_from_json(const json& b) {
    AsiairFields f;
    const auto S = [&](const char* k, std::optional<std::string>& o) {
        if (b.contains(k) && b[k].is_string()) o = b[k].get<std::string>();
    };
    const auto I = [&](const char* k, std::optional<int>& o) {
        if (b.contains(k) && b[k].is_number_integer()) o = b[k].get<int>();
    };
    const auto B = [&](const char* k, std::optional<bool>& o) {
        if (b.contains(k) && b[k].is_boolean()) o = b[k].get<bool>();
    };
    S("name", f.name); S("host", f.host); S("smb_share", f.smb_share); B("enabled", f.enabled);
    S("copy_mode", f.copy_mode); I("copy_n", f.copy_n); S("copy_time", f.copy_time);
    B("delete_after_copy", f.delete_after_copy); S("delete_mode", f.delete_mode);
    I("delete_n", f.delete_n); S("delete_time", f.delete_time); S("delete_via", f.delete_via);
    S("ssh_user", f.ssh_user); S("timezone", f.timezone); S("notes", f.notes);
    return f;
}

json rows_to_json(const std::vector<std::vector<std::string>>& rows,
                  const std::vector<std::string>& cols) {
    json out = json::array();
    for (const auto& r : rows) {
        json o = json::object();
        for (size_t i = 0; i < cols.size() && i < r.size(); ++i) o[cols[i]] = r[i];
        out.push_back(std::move(o));
    }
    return out;
}

void send_json(httplib::Response& res, int status, const json& j) {
    res.status = status;
    res.set_content(j.dump(), "application/json");
}

void send_err(httplib::Response& res, int status, const std::string& msg) {
    send_json(res, status, json{{"error", msg}});
}

bool valid_id(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (const char c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) return false;
    }
    return true;
}

}  // namespace

struct HttpServer::Impl {
    Config cfg;
    DbConfig dbcfg;
    std::string token;
    std::unique_ptr<Database> db;
    std::mutex dbmtx;
    std::unique_ptr<httplib::Server> srv;
    std::thread th;

    bool authed(const httplib::Request& req, httplib::Response& res) {
        if (token.empty()) return true;
        const std::string h = req.get_header_value("Authorization");
        const std::string pfx = "Bearer ";
        if (h.rfind(pfx, 0) == 0 && h.substr(pfx.size()) == token) return true;
        send_err(res, 401, "unauthorized");
        return false;
    }
};

HttpServer::HttpServer(Config cfg, DbConfig dbcfg) : impl_(std::make_unique<Impl>()) {
    impl_->cfg = std::move(cfg);
    impl_->dbcfg = std::move(dbcfg);
    if (const char* t = std::getenv("AIRWATCHER_TOKEN")) impl_->token = t;
    impl_->db = std::make_unique<Database>(impl_->dbcfg);

    if (impl_->cfg.web_tls) {
        std::string info;
        nightwatcher::ensure_self_signed_cert(impl_->cfg.web_tls_cert, impl_->cfg.web_tls_key,
                                              "airwatcher", info);
        impl_->srv = std::make_unique<httplib::SSLServer>(impl_->cfg.web_tls_cert.c_str(),
                                                          impl_->cfg.web_tls_key.c_str());
    } else {
        impl_->srv = std::make_unique<httplib::Server>();
    }

    Impl* im = impl_.get();
    httplib::Server& s = *im->srv;

    s.Get("/api/health", [im](const httplib::Request&, httplib::Response& res) {
        send_json(res, 200, json{{"ok", true}, {"version", AIRWATCHER_VERSION},
                                 {"auth", !im->token.empty()}});
    });

    s.Get("/api/asiairs", [im](const httplib::Request& req, httplib::Response& res) {
        if (!im->authed(req, res)) return;
        std::lock_guard<std::mutex> lk(im->dbmtx);
        json arr = json::array();
        for (const auto& a : im->db->asiairs()) arr.push_back(to_json(a));
        send_json(res, 200, arr);
    });

    s.Post("/api/asiairs", [im](const httplib::Request& req, httplib::Response& res) {
        if (!im->authed(req, res)) return;
        const json b = json::parse(req.body, nullptr, false);
        if (b.is_discarded() || !b.contains("id") || !b["id"].is_string()) {
            return send_err(res, 400, "id required");
        }
        const std::string id = b["id"];
        if (!valid_id(id)) return send_err(res, 400, "id must be [A-Za-z0-9_-], <= 64 chars");
        const AsiairFields f = fields_from_json(b);
        if (!f.host || f.host->empty()) return send_err(res, 400, "host required");
        std::lock_guard<std::mutex> lk(im->dbmtx);
        im->db->upsert_asiair(id, f);
        const auto a = im->db->find_asiair(id);
        send_json(res, 201, to_json(*a));
    });

    s.Get(R"(/api/asiairs/([^/]+))", [im](const httplib::Request& req, httplib::Response& res) {
        if (!im->authed(req, res)) return;
        std::lock_guard<std::mutex> lk(im->dbmtx);
        const auto a = im->db->find_asiair(req.matches[1]);
        if (!a) return send_err(res, 404, "no such asiair");
        send_json(res, 200, to_json(*a));
    });

    s.Patch(R"(/api/asiairs/([^/]+))", [im](const httplib::Request& req, httplib::Response& res) {
        if (!im->authed(req, res)) return;
        const json b = json::parse(req.body, nullptr, false);
        if (b.is_discarded()) return send_err(res, 400, "invalid JSON");
        std::lock_guard<std::mutex> lk(im->dbmtx);
        if (!im->db->update_asiair(req.matches[1], fields_from_json(b))) {
            return send_err(res, 404, "no such asiair");
        }
        send_json(res, 200, to_json(*im->db->find_asiair(req.matches[1])));
    });

    s.Delete(R"(/api/asiairs/([^/]+))", [im](const httplib::Request& req, httplib::Response& res) {
        if (!im->authed(req, res)) return;
        const std::string id = req.matches[1];
        std::lock_guard<std::mutex> lk(im->dbmtx);
        if (!im->db->remove_asiair(id)) return send_err(res, 404, "no such asiair");
        im->db->clear_status(id);
        send_json(res, 200, json{{"deleted", id}});
    });

    const auto set_enabled = [im](const httplib::Request& req, httplib::Response& res, bool on) {
        if (!im->authed(req, res)) return;
        AsiairFields f;
        f.enabled = on;
        std::lock_guard<std::mutex> lk(im->dbmtx);
        if (!im->db->update_asiair(req.matches[1], f)) return send_err(res, 404, "no such asiair");
        send_json(res, 200, to_json(*im->db->find_asiair(req.matches[1])));
    };
    s.Post(R"(/api/asiairs/([^/]+)/enable)",
           [set_enabled](const httplib::Request& req, httplib::Response& res) {
               set_enabled(req, res, true);
           });
    s.Post(R"(/api/asiairs/([^/]+)/disable)",
           [set_enabled](const httplib::Request& req, httplib::Response& res) {
               set_enabled(req, res, false);
           });

    // Connectivity self-test: can we reach the share and see Autorun?
    s.Post(R"(/api/asiairs/([^/]+)/test)", [im](const httplib::Request& req, httplib::Response& res) {
        if (!im->authed(req, res)) return;
        AsiairRow air;
        {
            std::lock_guard<std::mutex> lk(im->dbmtx);
            const auto a = im->db->find_asiair(req.matches[1]);
            if (!a) return send_err(res, 404, "no such asiair");
            air = *a;
        }
        const std::string share = air.smb_share.empty() ? "EMMC Images" : air.smb_share;
        json r{{"host", air.host}, {"share", share}};
        try {
            SmbClient c(air.host, share);
            const bool ok = c.reachable();
            r["reachable"] = ok;
            if (ok) r["autorun_files"] = c.walk("Autorun", {".fit", ".fits"}).size();
        } catch (const std::exception& e) {
            r["reachable"] = false;
            r["error"] = e.what();
        }
        send_json(res, 200, r);
    });

    s.Get("/api/discover", [im](const httplib::Request& req, httplib::Response& res) {
        if (!im->authed(req, res)) return;
        std::string cidr = req.get_param_value("cidr");
        if (cidr.empty()) cidr = im->cfg.discover_subnet;
        if (cidr.empty()) return send_err(res, 400, "cidr required (a.b.c.d/prefix)");
        try {
            json arr = json::array();
            for (const auto& d : discover(cidr)) {
                arr.push_back(json{{"ip", d.ip}, {"name", d.name}, {"smb_open", d.smb_open}});
            }
            send_json(res, 200, arr);
        } catch (const std::exception& e) {
            send_err(res, 400, e.what());
        }
    });

    s.Get("/api/status", [im](const httplib::Request& req, httplib::Response& res) {
        if (!im->authed(req, res)) return;
        std::lock_guard<std::mutex> lk(im->dbmtx);
        send_json(res, 200,
                  rows_to_json(im->db->status_rows(),
                               {"asiair", "ip", "enabled", "total_autorun", "remaining", "state",
                                "last_copy_utc", "updated_at"}));
    });

    s.Get("/api/log", [im](const httplib::Request& req, httplib::Response& res) {
        if (!im->authed(req, res)) return;
        int limit = 200;
        if (req.has_param("limit")) limit = std::atoi(req.get_param_value("limit").c_str());
        std::lock_guard<std::mutex> lk(im->dbmtx);
        send_json(res, 200,
                  rows_to_json(im->db->recent_log(limit),
                               {"ts_utc", "asiair", "action", "file", "status", "detail"}));
    });

    if (!impl_->cfg.web_root.empty()) {
        s.set_mount_point("/", impl_->cfg.web_root);
    }
}

HttpServer::~HttpServer() { stop(); }

void HttpServer::start() {
    Impl* im = impl_.get();
    const std::string bind = im->cfg.web_bind;
    const int port = im->cfg.web_port;
    im->th = std::thread([im, bind, port] {
        log_info("web UI listening on " + std::string(im->cfg.web_tls ? "https://" : "http://") +
                 bind + ":" + std::to_string(port) +
                 (im->token.empty() ? " (no token set)" : " (token required)"));
        if (!im->srv->listen(bind.c_str(), port)) {
            log_error("web UI failed to bind " + bind + ":" + std::to_string(port));
        }
    });
}

void HttpServer::stop() {
    Impl* im = impl_.get();
    if (im->srv) im->srv->stop();
    if (im->th.joinable()) im->th.join();
}

}  // namespace airwatcher
