/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "postflight/flight_log.hpp"

#include <json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace postflight {

FlightLog parse_flight_log(const std::string &json_text) {
  json doc;
  try {
    doc = json::parse(json_text);
  } catch (const std::exception &ex) {
    throw std::runtime_error(std::string("flight log JSON parse error: ") +
                             ex.what());
  }
  if (!doc.is_object())
    throw std::runtime_error("flight log: root is not an object");

  FlightLog fl;
  fl.version = doc.value("version", 0);
  fl.date = doc.value("date", std::string());
  fl.departure_icao = doc.value("departure_icao", std::string());
  fl.arrival_icao = doc.value("arrival_icao", std::string());
  fl.aircraft_icao = doc.value("aircraft_icao", std::string());
  fl.aircraft_tail = doc.value("aircraft_tail", std::string());
  fl.start_time = doc.value("start_time", std::int64_t{0});
  fl.end_time = doc.value("end_time", std::int64_t{0});
  fl.block_time_min = doc.value("block_time_min", 0);
  fl.max_altitude_ft = doc.value("max_altitude_ft", 0);
  fl.max_speed_kts = doc.value("max_speed_kts", 0);

  if (auto it = doc.find("track"); it != doc.end() && it->is_array()) {
    fl.track.reserve(it->size());
    for (const auto &p : *it) {
      if (!p.is_object())
        continue;
      TrackPoint tp;
      tp.t = p.value("t", std::int64_t{0});
      tp.lat = p.value("lat", 0.0);
      tp.lon = p.value("lon", 0.0);
      tp.alt_ft = p.value("alt", 0);
      tp.spd_kts = p.value("spd", 0);
      tp.vs_fpm = p.value("vs", 0);
      fl.track.push_back(tp);
    }
  }

  if (auto it = doc.find("landings"); it != doc.end() && it->is_array()) {
    fl.landings.reserve(it->size());
    for (const auto &l : *it) {
      if (!l.is_object())
        continue;
      Landing ld;
      ld.time = l.value("time", std::int64_t{0});
      ld.rating = l.value("rating", std::string());
      ld.fpm = l.value("fpm", 0.0);
      ld.g_force = l.value("g_force", 0.0);
      ld.bounce_count = l.value("bounce_count", 0);
      ld.flare = l.value("flare", std::string());
      ld.crosswind_kts = l.value("crosswind_kts", 0);
      ld.wind_status = l.value("wind_status", std::string());
      fl.landings.push_back(std::move(ld));
    }
  }

  return fl;
}

FlightLog load_flight_log(const std::string &path) {
  std::ifstream in(path);
  if (!in.good())
    throw std::runtime_error("flight log: cannot open " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return parse_flight_log(ss.str());
}

} // namespace postflight
