// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          tests/db_smoke.cpp
// Purpose:       Live round-trip check of the AirWatcher database layer against a
//                real MariaDB (env NWDB_PASSWORD etc.). Creates the schema, does
//                an asiair + file-state + status + extension round-trip, cleans up.
// Created:       2026-07-22
// Last Modified: 2026-07-22
// Version:       0.1.0
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>

#include "database.hpp"

using namespace airwatcher;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("CHECK failed: %s (line %d)\n", #c, __LINE__); ++g_fail; } } while (0)

int main() {
    try {
        Database db(DbConfig::from_env());
        db.ensure_schema();

        const std::string ID = "smoke-air";
        AsiairFields f;
        f.name = "Smoke Test Air"; f.host = "10.0.0.1"; f.smb_share = "EMMC Images";
        f.enabled = true; f.copy_mode = "batch"; f.copy_n = 5; f.delete_via = "none";
        db.upsert_asiair(ID, f);

        auto got = db.find_asiair(ID);
        CHECK(got.has_value());
        CHECK(got && got->name == "Smoke Test Air");
        CHECK(got && got->host == "10.0.0.1");
        CHECK(got && got->enabled);
        CHECK(got && got->copy_mode == "batch");
        CHECK(got && got->copy_n == 5);
        CHECK(db.active_asiairs().size() >= 1);

        // partial edit
        AsiairFields e; e.copy_n = 9; e.enabled = false;
        CHECK(db.update_asiair(ID, e));
        got = db.find_asiair(ID);
        CHECK(got && got->copy_n == 9 && !got->enabled);

        // file state
        db.mark_seen(ID, "Autorun/Light/M31/x_001.fit", 12345);
        db.mark_seen(ID, "Autorun/Light/M31/x_002.fit", 23456);
        db.mark_seen(ID, "Autorun/Light/M31/x_002.fit", 23456);  // idempotent
        db.mark_copied(ID, "Autorun/Light/M31/x_001.fit");
        CHECK(db.copied_paths(ID).size() == 1);
        CHECK(db.copied_not_deleted(ID).size() == 1);
        db.mark_deleted(ID, "Autorun/Light/M31/x_001.fit");
        CHECK(db.copied_not_deleted(ID).empty());

        // status snapshot (the registered data_table)
        db.upsert_status(ID, "10.0.0.1", true, 2, 1, "copying", true);
        db.upsert_status(ID, "10.0.0.1", true, 2, 0, "idle", false);  // upsert path

        // extension registry + events + action log
        db.register_extension("airwatcher-smoke", "AirWatcher", "0.1.0", "airwatcher_status");
        db.heartbeat("airwatcher-smoke");
        db.log_event("airwatcher", "info", "selftest", "db smoke", ID);
        db.log_action(ID, "copy", "Autorun/Light/M31/x_001.fit", "ok", "");
        CHECK(!db.recent_log(10).empty());

        // cleanup
        CHECK(db.remove_asiair(ID));
        CHECK(!db.find_asiair(ID));
        db.clear_status(ID);
        db.deregister("airwatcher-smoke");

        std::printf("db_smoke: %s (%d check failures)\n", g_fail ? "FAIL" : "OK", g_fail);
        return g_fail ? 1 : 0;
    } catch (const std::exception& e) {
        std::printf("db_smoke: EXCEPTION: %s\n", e.what());
        return 2;
    }
}
