/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef AIRPORTS_APT_DAT_PARSER_HPP
#define AIRPORTS_APT_DAT_PARSER_HPP

#include "airports/airport_data.hpp"

#include <functional>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

// SDK-free X-Plane apt.dat parser. Locating the file (XPLMGetSystemPath) and
// caching the result belong to the plugin / caching layer (issue #4); this
// module only turns an apt.dat byte stream into airport structs and is fully
// unit-testable. Handles apt.dat format versions 1100 and 1200.
namespace airports {

// DACH ICAO prefix filter: Germany (ED/ET — ET is military), Switzerland (LS),
// Austria (LO), Czechia (LZ). Matches the project's DACH scope in CLAUDE.md.
bool is_dach_airport(std::string_view icao);

// Parse the leading line code (e.g. 1, 100, 1054). Returns -1 when the line
// does not start with 1-4 digits followed by whitespace/end.
int parse_line_code(const std::string &line);

// Refine a base frequency type using its apt.dat name. apt.dat files file
// German AFIS/Info and Radio facilities under the Tower code (1054); the name
// suffix is the only discriminator. Only TOWER is refined.
FrequencyType classify_by_name(FrequencyType base, const std::string &name);

// Derive the operational facility type from a frequency table.
FacilityType classify_facility(const std::vector<Frequency> &freqs);

// Parse a full apt.dat stream. Only airports for which accept(icao) returns
// true are materialised (their child records are otherwise skipped), so the
// default DACH filter keeps memory bounded over a ~1 GB global file.
std::vector<Airport> parse_apt_dat(
    std::istream &in,
    const std::function<bool(std::string_view)> &accept = is_dach_airport);

} // namespace airports

#endif
