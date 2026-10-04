// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/copier/fits_check.cpp
// Purpose:       Implementation of fits_whole(): parse the primary header's
//                BITPIX/NAXIS cards, find END, compare the expected size with
//                the file size. No FITS library needed.
// Created:       2026-10-04
// Last Modified: 2026-10-04
// Version:       0.1.1
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#include "fits_check.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace airwatcher {

namespace {

constexpr long long kBlock = 2880;
constexpr int kMaxHeaderBlocks = 128;
constexpr std::streamsize kTailProbe = 64 * 1024;

// Integer value of a "KEY     = value / comment" card.
bool card_int(const std::string& card, long long& out) {
    if (card.size() < 11 || card[8] != '=' || card[9] != ' ') return false;
    std::string v = card.substr(10);
    const auto slash = v.find('/');
    if (slash != std::string::npos) v.erase(slash);
    const auto a = v.find_first_not_of(' ');
    if (a == std::string::npos) return false;
    const auto b = v.find_last_not_of(' ');
    v = v.substr(a, b - a + 1);
    char* end = nullptr;
    const long long n = std::strtoll(v.c_str(), &end, 10);
    if (end == v.c_str() || *end != '\0') return false;
    out = n;
    return true;
}

bool all_of_byte(const std::string& s, char c) {
    return s.find_first_not_of(c) == std::string::npos;
}

}  // namespace

bool fits_whole(const std::string& path, std::string& why) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        why = "cannot open";
        return false;
    }
    f.seekg(0, std::ios::end);
    const long long size = static_cast<long long>(f.tellg());
    f.seekg(0);

    long long hdr_bytes = -1;
    long long bitpix = 0;
    long long naxis = -1;
    std::vector<long long> axes(10, 0);       // NAXIS1..NAXIS9 at index 1..9
    std::string block(static_cast<size_t>(kBlock), '\0');
    for (int b = 0; b < kMaxHeaderBlocks && hdr_bytes < 0; ++b) {
        if (!f.read(&block[0], kBlock)) break;   // header runs past end of file
        for (long long i = 0; i < kBlock; i += 80) {
            const std::string card = block.substr(static_cast<size_t>(i), 80);
            const std::string key = card.substr(0, 8);
            long long v = 0;
            if (key == "END     ") {
                hdr_bytes = (b + 1) * kBlock;
                break;
            }
            if (key == "BITPIX  " && card_int(card, v)) {
                bitpix = v < 0 ? -v : v;
            } else if (key == "NAXIS   " && card_int(card, v)) {
                naxis = v;
            } else if (key.compare(0, 5, "NAXIS") == 0 && key[5] >= '1' && key[5] <= '9' &&
                       key[6] == ' ' && card_int(card, v)) {
                axes[static_cast<size_t>(key[5] - '0')] = v;
            }
        }
    }
    if (hdr_bytes < 0) {
        why = "no END card in the header";
        return false;
    }
    if (bitpix == 0 || naxis < 0) {
        why = "BITPIX/NAXIS cards missing";
        return false;
    }
    long long npix = 1;
    for (int ax = 1; ax <= naxis && ax <= 9; ++ax) npix *= axes[static_cast<size_t>(ax)];
    const long long data = naxis == 0 ? 0 : (bitpix / 8) * npix;
    const long long padded = (data + kBlock - 1) / kBlock * kBlock;
    const long long expected = hdr_bytes + padded;
    if (size < expected) {
        why = "short: " + std::to_string(size) + " of " + std::to_string(expected) + " bytes";
        return false;
    }
    if (size == expected) return true;

    f.clear();
    f.seekg(expected);
    const auto probe = static_cast<std::streamsize>(std::min<long long>(size - expected, kTailProbe));
    std::string tail(static_cast<size_t>(probe), '\0');
    f.read(&tail[0], probe);
    if (tail.compare(0, 8, "XTENSION") == 0) return true;            // a real extension
    if (all_of_byte(tail, '\0') || all_of_byte(tail, ' ')) return true;  // blank padding
    why = std::to_string(size - expected) + " bytes of trailing data after the image";
    return false;
}

}  // namespace airwatcher
