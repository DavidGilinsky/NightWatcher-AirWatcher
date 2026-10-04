// ---------------------------------------------------------------------------
// Author:        David Gilinsky
// File:          src/copier/fits_check.hpp
// Purpose:       Byte-level check that a landed file is one whole FITS image:
//                an END card, and exactly header + padded data on disk.
// Created:       2026-10-04
// Last Modified: 2026-10-04
// Version:       0.1.1
// License:       GPL-3.0-or-later
// ---------------------------------------------------------------------------
#pragma once

#include <string>

namespace airwatcher {

// True when `path` is a whole single-image FITS file: an END card within the
// first 128 header blocks, and a size equal to header + padded data (a real
// extension or blank padding after the data is tolerated). On false, `why`
// says what is wrong in a few words. A torn copy of a frame the ASIAir was
// rewriting fails this with "bytes of trailing data" or "no END card".
bool fits_whole(const std::string& path, std::string& why);

}  // namespace airwatcher
