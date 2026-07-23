// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/discovery/discovery.cpp
// Purpose:       Implementation of the ASIAir subnet scan (CIDR enumeration +
//                concurrent non-blocking TCP probes). Mirrors NightWatcher2's SQM
//                discovery thread-pool, with an ASIAir-specific probe.
// Created:       2026-07-22
// Last Modified: 2026-07-22
// Version:       0.1.0
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#include "discovery.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace airwatcher {

std::vector<std::string> cidr_hosts(const std::string& cidr) {
    const auto slash = cidr.find('/');
    if (slash == std::string::npos) {
        throw std::runtime_error("CIDR must be a.b.c.d/prefix: " + cidr);
    }
    const std::string addr = cidr.substr(0, slash);
    int prefix = 0;
    try {
        prefix = std::stoi(cidr.substr(slash + 1));
    } catch (...) {
        throw std::runtime_error("bad CIDR prefix: " + cidr);
    }
    if (prefix < 0 || prefix > 32) throw std::runtime_error("CIDR prefix out of range: " + cidr);

    in_addr ia{};
    if (inet_pton(AF_INET, addr.c_str(), &ia) != 1) {
        throw std::runtime_error("bad CIDR address: " + addr);
    }
    const uint32_t base = ntohl(ia.s_addr);
    const uint32_t mask = (prefix == 0) ? 0u : (~0u << (32 - prefix));
    const uint32_t network = base & mask;
    const uint32_t broadcast = network | ~mask;

    uint32_t first = network;
    uint32_t last = broadcast;
    if (prefix <= 30) {
        first = network + 1;
        last = broadcast - 1;
    }
    if (static_cast<uint64_t>(last) - first + 1 > 65536) {
        throw std::runtime_error("CIDR too large (> 65536 hosts): " + cidr);
    }

    std::vector<std::string> out;
    for (uint32_t a = first;; ++a) {
        in_addr o{};
        o.s_addr = htonl(a);
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &o, buf, sizeof buf);
        out.emplace_back(buf);
        if (a == last) break;
    }
    return out;
}

bool tcp_open(const std::string& ip, int port, int timeout_ms) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) return false;

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) {
        ::close(fd);
        return false;
    }

    bool ok = false;
    const int r = ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
    if (r == 0) {
        ok = true;
    } else if (errno == EINPROGRESS) {
        pollfd p{fd, POLLOUT, 0};
        if (::poll(&p, 1, timeout_ms) > 0 && (p.revents & POLLOUT)) {
            int err = 0;
            socklen_t len = sizeof err;
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                ok = true;
            }
        }
    }
    ::close(fd);
    return ok;
}

namespace {

std::string reverse_name(const std::string& ip) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) return "";
    char host[NI_MAXHOST];
    if (::getnameinfo(reinterpret_cast<sockaddr*>(&a), sizeof a, host, sizeof host, nullptr, 0,
                      NI_NAMEREQD) == 0) {
        return host;
    }
    return "";
}

uint32_t ip_num(const std::string& ip) {
    in_addr ia{};
    inet_pton(AF_INET, ip.c_str(), &ia);
    return ntohl(ia.s_addr);
}

}  // namespace

std::vector<Discovered> discover(const std::string& cidr, int port, int timeout_ms,
                                 int concurrency) {
    const std::vector<std::string> hosts = cidr_hosts(cidr);
    std::vector<Discovered> results;
    if (hosts.empty()) return results;

    std::mutex mtx;
    std::atomic<size_t> next{0};
    const int nthreads = std::max(1, std::min<int>(concurrency, static_cast<int>(hosts.size())));

    const auto worker = [&]() {
        size_t i = 0;
        while ((i = next.fetch_add(1)) < hosts.size()) {
            const std::string& ip = hosts[i];
            if (!tcp_open(ip, port, timeout_ms)) continue;  // no ASIAir control port
            Discovered d;
            d.ip = ip;
            d.smb_open = tcp_open(ip, 445, timeout_ms);
            d.name = reverse_name(ip);
            std::lock_guard<std::mutex> lk(mtx);
            results.push_back(std::move(d));
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(nthreads);
    for (int t = 0; t < nthreads; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    std::sort(results.begin(), results.end(),
              [](const Discovered& a, const Discovered& b) { return ip_num(a.ip) < ip_num(b.ip); });
    return results;
}

}  // namespace airwatcher
