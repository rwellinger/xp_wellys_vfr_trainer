/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef POSTFLIGHT_DISCOVERY_HPP
#define POSTFLIGHT_DISCOVERY_HPP

#include "postflight/atc_log.hpp"
#include "postflight/flight_log.hpp"

#include <cstdint>
#include <optional>
#include <string>

// Locate the JSON files to evaluate. Both producers drop one file per flight
// into their own Output subdirectory (resolved by src/dependencies from the
// X-Plane root); the trainer picks the newest in each and confirms the two
// describe the same flight. SDK-free — the plugin layer passes the directory
// paths from deps::DependencyState::output_path.
namespace postflight {

// Absolute path of the most recently modified "*.json" file in `dir`, or
// nullopt when the directory is missing / empty / unreadable.
std::optional<std::string> newest_json(const std::string &dir);

// Sanity check that a newest ATC file and newest flight-log file belong to the
// same flight: departure ICAOs match (and destinations match when both are
// set), and the two open timestamps are within `tolerance_s` (the ATC
// started_at_epoch is frozen at flight open, the flight-log start_time at
// Idle->Rolling, so a few minutes of taxi difference is expected).
bool same_flight(const AtcLog &atc, const FlightLog &flight,
                 std::int64_t tolerance_s = 3600);

} // namespace postflight

#endif // POSTFLIGHT_DISCOVERY_HPP
