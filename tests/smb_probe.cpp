// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          tests/smb_probe.cpp
// Purpose:       Manual SMB client probe against a share (a local mock ASIAir or
//                the real device). Usage: smb_probe HOST SHARE [USER] [PASS] [WALKDIR]
// Created:       2026-07-22
// Last Modified: 2026-07-22
// Version:       0.1.0
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#include <cstdio>
#include <string>

#include "smb_client.hpp"

int main(int argc, char** argv) {
    const std::string host  = argc > 1 ? argv[1] : "127.0.0.1";
    const std::string share = argc > 2 ? argv[2] : "EMMC Images";
    const std::string user  = argc > 3 ? argv[3] : "";
    const std::string pass  = argc > 4 ? argv[4] : "";
    const std::string dir   = argc > 5 ? argv[5] : "Autorun";
    try {
        airwatcher::SmbClient c(host, share, user, pass);
        std::printf("reachable: %s\n", c.reachable() ? "yes" : "no");

        const auto files = c.walk(dir, {".fit", ".fits"});
        std::printf("walk %s -> %zu FITS file(s)\n", dir.c_str(), files.size());
        for (size_t i = 0; i < files.size() && i < 6; ++i) {
            std::printf("  %s (%lld bytes)\n", files[i].path.c_str(), files[i].size);
        }
        if (!files.empty()) {
            const long long n = c.copy_to(files[0].path, "/tmp/aw_smb_probe_copy.bin");
            std::printf("copy_to: %lld bytes from %s\n", n, files[0].path.c_str());
            try {
                c.remove(files[0].path);
                std::printf("remove: SUCCEEDED (share is writable)\n");
            } catch (const std::exception& e) {
                std::printf("remove: failed as expected on a read-only share (%s)\n", e.what());
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::printf("ERROR: %s\n", e.what());
        return 1;
    }
}
