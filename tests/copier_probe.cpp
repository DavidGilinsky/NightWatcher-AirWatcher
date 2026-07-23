// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          tests/copier_probe.cpp
// Purpose:       End-to-end copier check: run cycles against a mock ASIAir SMB
//                share (env MOCK_HOST/MOCK_SHARE, default 127.0.0.1/'EMMC Images')
//                into a temp incoming dir; verify copy + idempotency. Needs nwdb.
// Created:       2026-07-22
// Last Modified: 2026-07-22
// Version:       0.1.0
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "copier.hpp"
#include "database.hpp"

namespace fs = std::filesystem;
using namespace airwatcher;

int main() {
    const char* host = std::getenv("MOCK_HOST");
    const char* share = std::getenv("MOCK_SHARE");
    const std::string inc = "/tmp/aw_copier_probe_incoming";
    fs::remove_all(inc);

    Config cfg;
    cfg.incoming = inc;
    cfg.poll_interval_s = 60;

    try {
        DbConfig dbc = DbConfig::from_env();
        Database db(dbc);
        db.ensure_schema();

        const std::string ID = "copier-probe-air";
        db.remove_asiair(ID);  // clean slate (clears prior file-state)
        AsiairFields f;
        f.host = host ? host : "127.0.0.1";
        f.smb_share = share ? share : "EMMC Images";
        f.enabled = true;
        f.copy_mode = "immediate";
        f.delete_via = "none";
        db.upsert_asiair(ID, f);
        const auto air = db.find_asiair(ID);

        Copier cop(cfg, dbc);
        const CycleResult r1 = cop.run_once(db, *air);
        std::printf("cycle1: total=%lld remaining=%lld copied=%d state=%s\n",
                    r1.total, r1.remaining, r1.copied, r1.state.c_str());
        const CycleResult r2 = cop.run_once(db, *air);
        std::printf("cycle2: total=%lld remaining=%lld copied=%d state=%s (idempotent)\n",
                    r2.total, r2.remaining, r2.copied, r2.state.c_str());

        int on_disk = 0;
        for (const auto& e : fs::recursive_directory_iterator(inc)) {
            if (e.is_regular_file() && e.path().extension() != ".part") ++on_disk;
        }
        std::printf("files landed in incoming: %d (under %s/%s/)\n", on_disk, inc.c_str(), ID.c_str());

        // cleanup
        db.remove_asiair(ID);
        db.clear_status(ID);
        fs::remove_all(inc);

        const bool ok = r1.total == 4 && r1.copied == 4 && r1.remaining == 0 &&
                        r2.copied == 0 && r2.remaining == 0 && on_disk == 4;
        std::printf("copier_probe: %s\n", ok ? "OK" : "FAIL");
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::printf("copier_probe: EXCEPTION: %s\n", e.what());
        return 2;
    }
}
