/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "postflight/discovery.hpp"

#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace postflight {

std::optional<std::string> newest_json(const std::string &dir) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec))
    return std::nullopt;

  std::string best;
  fs::file_time_type best_time{};
  bool found = false;

  for (const auto &entry : fs::directory_iterator(
           dir, fs::directory_options::skip_permission_denied, ec)) {
    if (ec)
      break;
    if (!entry.is_regular_file(ec))
      continue;
    const fs::path &p = entry.path();
    if (p.extension() != ".json")
      continue;
    const fs::file_time_type t = fs::last_write_time(p, ec);
    if (ec)
      continue;
    if (!found || t > best_time) {
      best_time = t;
      best = p.string();
      found = true;
    }
  }

  if (!found)
    return std::nullopt;
  return best;
}

bool same_flight(const AtcLog &atc, const FlightLog &flight,
                 std::int64_t tolerance_s) {
  if (atc.flight.departure_airport != flight.departure_icao)
    return false;
  if (!atc.flight.destination_airport.empty() &&
      atc.flight.destination_airport != flight.arrival_icao)
    return false;
  const std::int64_t d =
      std::llabs(atc.flight.started_at_epoch - flight.start_time);
  return d <= tolerance_s;
}

} // namespace postflight
