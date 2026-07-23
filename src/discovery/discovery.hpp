// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/discovery/discovery.hpp
// Purpose:       Subnet discovery of ASIAir devices: a concurrent CIDR scan for
//                the ASIAir control port (4400), confirmed by an open SMB port.
// Created:       2026-07-22
// Last Modified: 2026-07-22
// Version:       0.1.0
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

namespace airwatcher {

struct Discovered {
    std::string ip;
    std::string name;       // reverse-DNS / NetBIOS name if resolvable, else empty
    bool smb_open = false;  // TCP 445 also open (an ASIAir serves SMB)
};

// Enumerate usable host IPs in "a.b.c.d/prefix". Network + broadcast are excluded
// for prefix <= 30 (inclusive for /31, /32). Throws on a bad CIDR or > 65536 hosts.
std::vector<std::string> cidr_hosts(const std::string& cidr);

// Non-blocking TCP connect with a timeout; true if the port accepts.
bool tcp_open(const std::string& ip, int port, int timeout_ms);

// Scan a CIDR for ASIAirs: probe each host for `port` (4400 by default) with a
// bounded thread pool; confirmed hits also note whether SMB (445) is open and the
// reverse-resolved name. Results are sorted by numeric IP.
std::vector<Discovered> discover(const std::string& cidr, int port = 4400,
                                 int timeout_ms = 700, int concurrency = 128);

}  // namespace airwatcher
