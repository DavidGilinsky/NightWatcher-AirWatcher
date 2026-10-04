// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/smb/smb_client.cpp
// Purpose:       libsmbclient (Samba) wrapper implementation. Each SmbClient owns
//                a private SMBCCTX (thread-safe: one per worker); guest/anonymous
//                auth via a per-context callback.
// Created:       2026-07-22
// Last Modified: 2026-10-04
// Version:       0.1.1
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#include "smb_client.hpp"

#include <fcntl.h>
#include <libsmbclient.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace airwatcher {

namespace {

// User data handed to the auth callback via the context.
struct AuthData {
    std::string user;
    std::string pass;
};

void auth_cb(SMBCCTX* c, const char* /*srv*/, const char* /*shr*/,
             char* wg, int wglen, char* un, int unlen, char* pw, int pwlen) {
    auto* a = static_cast<AuthData*>(smbc_getOptionUserData(c));
    const auto put = [](char* b, int len, const std::string& v) {
        if (len > 0) {
            std::strncpy(b, v.c_str(), static_cast<size_t>(len) - 1);
            b[len - 1] = '\0';
        }
    };
    put(wg, wglen, "WORKGROUP");
    put(un, unlen, a ? a->user : std::string());
    put(pw, pwlen, a ? a->pass : std::string());
}

// Percent-encode everything but the RFC3986 unreserved set, keeping '/'.
std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    o.reserve(s.size());
    for (const unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            o += static_cast<char>(c);
        } else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 0x0F];
        }
    }
    return o;
}

bool ends_with_ci(const std::string& s, const std::string& suf) {
    if (suf.size() > s.size()) return false;
    for (size_t i = 0; i < suf.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[s.size() - suf.size() + i])) !=
            std::tolower(static_cast<unsigned char>(suf[i]))) {
            return false;
        }
    }
    return true;
}

bool matches(const std::string& path, const std::vector<std::string>& suffixes) {
    if (suffixes.empty()) return true;
    for (const auto& suf : suffixes) {
        if (ends_with_ci(path, suf)) return true;
    }
    return false;
}

}  // namespace

struct SmbClient::Ctx {
    SMBCCTX* ctx = nullptr;
    AuthData auth;
};

SmbClient::SmbClient(std::string host, std::string share, std::string user,
                     std::string password, std::string conf_path)
    : ctx_(std::make_unique<Ctx>()), host_(std::move(host)), share_(std::move(share)) {
    ctx_->auth.user = std::move(user);
    ctx_->auth.pass = std::move(password);

    SMBCCTX* c = smbc_new_context();
    if (c == nullptr) throw std::runtime_error("smbc_new_context failed");

    smbc_setOptionUserData(c, &ctx_->auth);
    smbc_setFunctionAuthDataWithContext(c, auth_cb);
    smbc_setOptionUseKerberos(c, 0);
    smbc_setOptionFallbackAfterKerberos(c, 0);
    smbc_setOptionNoAutoAnonymousLogin(c, 0);  // allow anonymous
    smbc_setDebug(c, 0);
    if (!conf_path.empty()) smbc_setConfiguration(c, conf_path.c_str());

    if (smbc_init_context(c) == nullptr) {
        smbc_free_context(c, 1);
        throw std::runtime_error("smbc_init_context failed");
    }
    ctx_->ctx = c;
}

SmbClient::~SmbClient() {
    if (ctx_ && ctx_->ctx) smbc_free_context(ctx_->ctx, 1);
}

std::string SmbClient::url_for(const std::string& rel) const {
    std::string u = "smb://" + host_ + "/" + url_encode(share_);
    if (!rel.empty()) u += "/" + url_encode(rel);
    return u;
}

std::vector<SmbEntry> SmbClient::list(const std::string& rel_dir) {
    SMBCCTX* c = ctx_->ctx;
    const std::string dirurl = url_for(rel_dir);
    SMBCFILE* dh = smbc_getFunctionOpendir(c)(c, dirurl.c_str());
    if (dh == nullptr) {
        throw std::runtime_error("opendir '" + (rel_dir.empty() ? std::string("/") : rel_dir) +
                                 "': " + std::strerror(errno));
    }
    std::vector<SmbEntry> out;
    struct smbc_dirent* de = nullptr;
    while ((de = smbc_getFunctionReaddir(c)(c, dh)) != nullptr) {
        const std::string name(de->name);
        if (name == "." || name == "..") continue;
        if (de->smbc_type != SMBC_DIR && de->smbc_type != SMBC_FILE) continue;
        SmbEntry e;
        e.is_dir = (de->smbc_type == SMBC_DIR);
        e.path = rel_dir.empty() ? name : (rel_dir + "/" + name);
        if (!e.is_dir) {
            struct stat st{};
            if (smbc_getFunctionStat(c)(c, url_for(e.path).c_str(), &st) == 0) {
                e.size = static_cast<long long>(st.st_size);
                e.mtime = static_cast<long long>(st.st_mtime);
            }
        }
        out.push_back(std::move(e));
    }
    smbc_getFunctionClosedir(c)(c, dh);
    return out;
}

std::vector<SmbEntry> SmbClient::walk(const std::string& rel_dir,
                                      const std::vector<std::string>& suffixes) {
    std::vector<SmbEntry> out;
    std::vector<std::string> stack{rel_dir};
    while (!stack.empty()) {
        const std::string dir = std::move(stack.back());
        stack.pop_back();
        std::vector<SmbEntry> entries;
        try {
            entries = list(dir);
        } catch (const std::exception&) {
            continue;  // an unreadable subtree is skipped, not fatal
        }
        for (auto& e : entries) {
            if (e.is_dir) {
                stack.push_back(e.path);
            } else if (matches(e.path, suffixes)) {
                out.push_back(std::move(e));
            }
        }
    }
    return out;
}

SmbEntry SmbClient::stat(const std::string& rel_path) {
    SMBCCTX* c = ctx_->ctx;
    struct stat st{};
    if (smbc_getFunctionStat(c)(c, url_for(rel_path).c_str(), &st) != 0) {
        throw std::runtime_error("stat '" + rel_path + "': " + std::strerror(errno));
    }
    SmbEntry e;
    e.path = rel_path;
    e.size = static_cast<long long>(st.st_size);
    e.mtime = static_cast<long long>(st.st_mtime);
    e.is_dir = S_ISDIR(st.st_mode);
    return e;
}

long long SmbClient::copy_to(const std::string& rel_path, const std::string& local_path) {
    SMBCCTX* c = ctx_->ctx;
    SMBCFILE* fh = smbc_getFunctionOpen(c)(c, url_for(rel_path).c_str(), O_RDONLY, 0);
    if (fh == nullptr) {
        throw std::runtime_error("open '" + rel_path + "': " + std::strerror(errno));
    }
    FILE* out = std::fopen(local_path.c_str(), "wb");
    if (out == nullptr) {
        smbc_getFunctionClose(c)(c, fh);
        throw std::runtime_error("create '" + local_path + "': " + std::strerror(errno));
    }
    char buf[1 << 16];
    long long total = 0;
    ssize_t n = 0;
    while ((n = smbc_getFunctionRead(c)(c, fh, buf, sizeof buf)) > 0) {
        if (std::fwrite(buf, 1, static_cast<size_t>(n), out) != static_cast<size_t>(n)) {
            std::fclose(out);
            smbc_getFunctionClose(c)(c, fh);
            throw std::runtime_error("write '" + local_path + "': " + std::strerror(errno));
        }
        total += n;
    }
    const int rerr = (n < 0) ? errno : 0;
    std::fclose(out);
    smbc_getFunctionClose(c)(c, fh);
    if (n < 0) throw std::runtime_error("read '" + rel_path + "': " + std::strerror(rerr));
    return total;
}

void SmbClient::remove(const std::string& rel_path) {
    SMBCCTX* c = ctx_->ctx;
    if (smbc_getFunctionUnlink(c)(c, url_for(rel_path).c_str()) != 0) {
        throw std::runtime_error("unlink '" + rel_path + "': " + std::strerror(errno));
    }
}

bool SmbClient::reachable() noexcept {
    try {
        SMBCCTX* c = ctx_->ctx;
        SMBCFILE* dh = smbc_getFunctionOpendir(c)(c, url_for("").c_str());
        if (dh == nullptr) return false;
        smbc_getFunctionClosedir(c)(c, dh);
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace airwatcher
