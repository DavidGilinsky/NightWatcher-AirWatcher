// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          tests/fits_check_test.cpp
// Purpose:       Unit checks for fits_whole(): a whole frame passes; a torn
//                copy (trailing block), a short file and a header without END
//                fail with the expected reason; blank padding is tolerated.
// Created:       2026-10-04
// Last Modified: 2026-10-04
// Version:       0.1.1
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "fits_check.hpp"

namespace fs = std::filesystem;
using airwatcher::fits_whole;

namespace {

std::string card(const std::string& key, const std::string& value) {
    std::string c = key;
    c.resize(8, ' ');
    c += "= ";
    std::string v = value;
    while (v.size() < 20) v.insert(v.begin(), ' ');
    c += v;
    c.resize(80, ' ');
    return c;
}

// A minimal 8x6 int16 image: one header block + one data block.
std::string whole_frame() {
    std::string h = card("SIMPLE", "T") + card("BITPIX", "16") + card("NAXIS", "2") +
                    card("NAXIS1", "8") + card("NAXIS2", "6") + "END";
    h.resize(2880, ' ');
    std::string d(96, '\x5a');
    d.resize(2880, '\0');
    return h + d;
}

void write(const fs::path& p, const std::string& bytes) {
    std::ofstream(p, std::ios::binary) << bytes;
}

int fails = 0;

void expect(bool cond, const std::string& what) {
    std::printf("%s  %s\n", cond ? "ok  " : "FAIL", what.c_str());
    if (!cond) ++fails;
}

}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "aw_fits_check_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string why;

    write(dir / "whole.fits", whole_frame());
    expect(fits_whole((dir / "whole.fits").string(), why), "whole frame passes");

    std::string torn_tail(960, '\x91');
    torn_tail.resize(2880, '\0');
    write(dir / "torn.fits", whole_frame() + torn_tail);
    expect(!fits_whole((dir / "torn.fits").string(), why) && why.find("trailing") != std::string::npos,
           "torn copy fails with trailing data: " + why);

    write(dir / "short.fits", whole_frame().substr(0, 2880 + 100));
    expect(!fits_whole((dir / "short.fits").string(), why) && why.find("short") == 0,
           "short file fails: " + why);

    std::string noend = whole_frame();
    noend.replace(noend.find("END"), 3, "   ");
    write(dir / "noend.fits", noend);
    expect(!fits_whole((dir / "noend.fits").string(), why) && why.find("END") != std::string::npos,
           "missing END fails: " + why);

    write(dir / "padded.fits", whole_frame() + std::string(2880, '\0'));
    expect(fits_whole((dir / "padded.fits").string(), why), "blank trailing padding tolerated");

    fs::remove_all(dir);
    std::printf("fits_check_test: %s\n", fails == 0 ? "OK" : "FAIL");
    return fails == 0 ? 0 : 1;
}
