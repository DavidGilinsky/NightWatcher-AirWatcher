// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          tests/discovery_probe.cpp
// Purpose:       Check cidr_hosts enumeration and run a discovery scan.
//                Usage: discovery_probe [CIDR] [PORT]
// Created:       2026-07-22
// Last Modified: 2026-07-22
// Version:       0.1.0
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <string>

#include "discovery.hpp"

using namespace airwatcher;

int main(int argc, char** argv) {
    int fails = 0;

    const auto h30 = cidr_hosts("192.168.9.0/30");   // .1 .2
    std::printf("cidr /30 -> %zu hosts (%s..%s)\n", h30.size(),
                h30.empty() ? "-" : h30.front().c_str(), h30.empty() ? "-" : h30.back().c_str());
    if (h30.size() != 2 || h30.front() != "192.168.9.1" || h30.back() != "192.168.9.2") ++fails;

    const auto h29 = cidr_hosts("10.1.2.0/29");       // .1 .. .6
    if (h29.size() != 6) ++fails;

    const auto h32 = cidr_hosts("172.22.4.5/32");     // exactly the host
    if (h32.size() != 1 || h32.front() != "172.22.4.5") ++fails;

    bool threw = false;
    try { cidr_hosts("10.0.0.0/8"); } catch (...) { threw = true; }
    if (!threw) ++fails;                               // > 65536 hosts must throw

    const std::string cidr = argc > 1 ? argv[1] : "127.0.0.1/32";
    const int port = argc > 2 ? std::atoi(argv[2]) : 445;  // 445 is open (mock Samba)
    const auto found = discover(cidr, port, 400, 64);
    std::printf("discover %s port %d -> %zu candidate(s)\n", cidr.c_str(), port, found.size());
    for (const auto& d : found) {
        std::printf("  %s  smb=%d  name=%s\n", d.ip.c_str(), d.smb_open ? 1 : 0, d.name.c_str());
    }

    std::printf("discovery_probe: %s (%d cidr_hosts failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
