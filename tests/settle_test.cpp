// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          tests/settle_test.cpp
// Purpose:       Unit checks for Copier::settled(): a file is offered for copy
//                only after its size and mtime have held still for
//                stable_seconds; any change restarts the clock; a failed
//                verification restarts it too. Uses an injected clock, no DB,
//                no SMB.
// Created:       2026-10-04
// Last Modified: 2026-10-04
// Version:       0.1.1
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#include <chrono>
#include <cstdio>
#include <string>

#include "copier.hpp"

using namespace airwatcher;
using clock_t_ = std::chrono::steady_clock;

namespace {

int fails = 0;

void expect(bool cond, const std::string& what) {
    std::printf("%s  %s\n", cond ? "ok  " : "FAIL", what.c_str());
    if (!cond) ++fails;
}

}  // namespace

int main() {
    Config cfg;
    cfg.stable_seconds = 60;
    Copier cop(cfg, DbConfig{});
    const std::string air = "air-test";
    SmbEntry e;
    e.path = "Autorun/Light/NGC 7720/Light_0007.fit";
    e.size = 89464320;
    e.mtime = 1791080588;
    const auto t0 = clock_t_::now();
    using std::chrono::seconds;

    expect(!cop.settled(air, e, t0), "first sight is never settled");
    expect(!cop.settled(air, e, t0 + seconds(30)), "30 s unchanged: not yet");
    expect(cop.settled(air, e, t0 + seconds(60)), "60 s unchanged: settled");

    // The ASIAir rewrites the file (new size + mtime): the clock restarts.
    e.size = 89467200;
    e.mtime += 20;
    expect(!cop.settled(air, e, t0 + seconds(61)), "changed file: not settled");
    expect(!cop.settled(air, e, t0 + seconds(100)), "39 s after the change: not yet");
    expect(cop.settled(air, e, t0 + seconds(121)), "60 s after the change: settled");

    // A failed verification restarts the clock and counts.
    expect(cop.verify_failed(air, e.path) == 1, "first verify failure counted");
    expect(!cop.settled(air, e, clock_t_::now()), "after a failure: must settle again");
    expect(cop.verify_failed(air, e.path) == 2, "second verify failure counted");

    // Another file on another ASIAir is independent.
    SmbEntry other = e;
    other.path = "Autorun/Light/M31/Light_0001.fit";
    expect(!cop.settled("air-2", other, t0), "independent file starts unsettled");
    expect(cop.settled("air-2", other, t0 + seconds(60)), "and settles on its own clock");

    std::printf("settle_test: %s\n", fails == 0 ? "OK" : "FAIL");
    return fails == 0 ? 0 : 1;
}
