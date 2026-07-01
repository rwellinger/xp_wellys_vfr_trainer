/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef POSTFLIGHT_FLIGHT_LOG_HPP
#define POSTFLIGHT_FLIGHT_LOG_HPP

#include <cstdint>
#include <string>
#include <vector>

// Parser for the flight-log JSON produced by the `xp_pilot` plugin (flight
// execution: track + landings). Field names mirror docs/flight_log_json.md.
// SDK-free (pure nlohmann::json + std), so the engine tests can parse the
// testdata fixture directly. Malformed input throws std::runtime_error.
namespace postflight {

// One position sample (~10 s interval). Times are Unix epoch seconds (UTC).
struct TrackPoint {
  std::int64_t t = 0; // epoch s (UTC)
  double lat = 0.0;
  double lon = 0.0;
  int alt_ft = 0;  // MSL
  int spd_kts = 0; // IAS (not ground speed)
  int vs_fpm = 0;
};

// One touchdown. Only the fields the evaluation prompt consumes are kept.
struct Landing {
  std::int64_t time = 0; // epoch s (UTC), nose-gear touchdown
  std::string rating;    // BUTTER! / GREAT LANDING! / ACCEPTABLE / ...
  double fpm = 0.0;      // touchdown vertical speed
  double g_force = 0.0;
  int bounce_count = 0;
  std::string flare;       // e.g. "Good, but late flare"
  int crosswind_kts = 0;
  std::string wind_status; // CALM / LIGHT / STEADY
};

struct FlightLog {
  int version = 0;
  std::string date; // YYYY-MM-DD (UTC)
  std::string departure_icao;
  std::string arrival_icao;
  std::string aircraft_icao;
  std::string aircraft_tail;
  std::int64_t start_time = 0; // epoch s (UTC)
  std::int64_t end_time = 0;   // epoch s (UTC)
  int block_time_min = 0;
  int max_altitude_ft = 0;
  int max_speed_kts = 0;
  std::vector<Landing> landings;
  std::vector<TrackPoint> track;
};

// Parse from a JSON string. Throws std::runtime_error on malformed input.
FlightLog parse_flight_log(const std::string &json_text);

// Read a file and parse it. Throws std::runtime_error on I/O or parse error.
FlightLog load_flight_log(const std::string &path);

} // namespace postflight

#endif // POSTFLIGHT_FLIGHT_LOG_HPP
