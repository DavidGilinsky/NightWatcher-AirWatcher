// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/smb/smb_client.hpp
// Purpose:       Thread-safe RAII wrapper over libsmbclient (Samba) for reading a
//                guest SMB share on an ASIAir: list/walk/copy/delete. Each client
//                owns its own SMBCCTX so one instance per worker thread is safe.
// Created:       2026-07-22
// Last Modified: 2026-07-22
// Version:       0.1.0
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace airwatcher {

struct SmbEntry {
    std::string path;       // path relative to the share root, '/'-separated
    long long   size = 0;
    bool        is_dir = false;
};

// A single SMB session to one share on one host. libsmbclient's context is not
// shared between instances, so a per-thread SmbClient is safe. Guest/anonymous
// auth by default (empty user + password), which is how the ASIAir share works.
class SmbClient {
public:
    // host:  ip or resolvable name (e.g. "10.0.0.1" or "asiair")
    // share: e.g. "EMMC Images" (spaces are handled)
    // user/password: empty = anonymous/guest
    // conf_path: optional smb.conf (e.g. to allow SMB1 on old firmware); empty = system default
    SmbClient(std::string host, std::string share,
              std::string user = "", std::string password = "",
              std::string conf_path = "");
    ~SmbClient();
    SmbClient(const SmbClient&) = delete;
    SmbClient& operator=(const SmbClient&) = delete;

    // List one directory (rel_dir relative to the share root; "" = root).
    std::vector<SmbEntry> list(const std::string& rel_dir);

    // Recursively collect FILES under rel_dir whose name ends with any of
    // `suffixes` (case-insensitive; empty list = every file). Paths are relative
    // to the share root. Unreadable subdirectories are skipped, not fatal.
    std::vector<SmbEntry> walk(const std::string& rel_dir,
                               const std::vector<std::string>& suffixes);

    // Copy a remote file (relative path) to local_path. Returns bytes copied.
    // Throws std::runtime_error on any I/O error.
    long long copy_to(const std::string& rel_path, const std::string& local_path);

    // Delete a remote file (relative path). Throws on failure -- notably the
    // ASIAir's stock share is read-only, so this fails there by design.
    void remove(const std::string& rel_path);

    // Can we open the share root? (connectivity/auth self-test; never throws)
    bool reachable() noexcept;

private:
    std::string url_for(const std::string& rel) const;

    struct Ctx;
    std::unique_ptr<Ctx> ctx_;
    std::string host_;
    std::string share_;
};

}  // namespace airwatcher
