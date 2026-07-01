/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef POSTFLIGHT_ATC_LOG_HPP
#define POSTFLIGHT_ATC_LOG_HPP

#include <cstdint>
#include <string>
#include <vector>

// Parser for the ATC transmission JSON produced by the `xp_wellys_vfr_atc`
// plugin (radio discipline / phraseology). Field names mirror
// docs/atc_transmission_json.md.
//
// v2-only: correlation with the flight log relies on the UTC-epoch fields
// (transmissions[].ts, flight.started_at_epoch) the ATC plugin emits since its
// #17. Files without them (version 1, local-time strings only) are rejected —
// the trainer never reconstructs the local->UTC offset (see
// docs/post_flight_correlation.md). Malformed / v1 input throws
// std::runtime_error.
namespace postflight {

// Required schema version. Bump only if the ATC producer breaks compatibility.
constexpr int kAtcLogVersion = 2;

struct Transmission {
  std::int64_t ts = 0;      // epoch s (UTC) — the correlation key
  std::string time;         // local-time string (informational, no timezone)
  std::string transcript;   // pilot radio call (whisper transcription)
  std::string atc_response; // controller reply ("" when none)
  std::string intent;       // classified intent enum
  double confidence = 0.0;
  std::string flight_phase; // PARKED / TAXI / PATTERN / CRUISE / ...
  std::string outcome;      // classified / unknown / tower_reported_garbled
  bool readback_issue = false; // readback_missing_elements non-empty
  std::string failure_locus;   // phraseology / proper_name / mixed / unclear / ""
};

struct AtcFlight {
  std::string started_at;            // local-time string (informational)
  std::int64_t started_at_epoch = 0; // epoch s (UTC), frozen at flight open
  std::string departure_airport;
  std::string destination_airport; // may be empty (null in source)
  std::string pilot_callsign;
  bool missing_initial_call = false;
};

struct AtcLog {
  int version = 0;
  AtcFlight flight;
  std::vector<Transmission> transmissions;
};

// Parse from a JSON string. Throws std::runtime_error when the input is
// malformed, is not version 2, or is missing the epoch fields required for
// correlation (flight.started_at_epoch, every transmissions[].ts).
AtcLog parse_atc_log(const std::string &json_text);

// Read a file and parse it. Throws std::runtime_error on I/O or parse error.
AtcLog load_atc_log(const std::string &path);

} // namespace postflight

#endif // POSTFLIGHT_ATC_LOG_HPP
