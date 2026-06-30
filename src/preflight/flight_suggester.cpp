/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "preflight/flight_suggester.hpp"

#include "airports/geo.hpp"

#include <algorithm>
#include <cmath>

namespace preflight {

namespace {

// GA-suitability thresholds.
constexpr float kMinRunwayM = 500.0f; // shortest usable GA runway length
constexpr double kMinFlightKm = 5.0;  // ignore trivially short hops

// apt.dat runway surface codes considered GA-usable: 1 asphalt, 2 concrete,
// 3 turf/grass, 4 dirt, 5 gravel. Excludes water (7), snow (8), lakebed (6),
// unknown (0).
bool surface_ga_usable(int surface_code) {
  return surface_code >= 1 && surface_code <= 5;
}

bool facility_is_endpoint(FacilityType f) {
  return f == FacilityType::TOWERED || f == FacilityType::AFIS;
}

// Does a (dep, dest) facility pair satisfy the requested ATC type?
bool atc_pair_ok(AtcType t, FacilityType dep, FacilityType dest) {
  switch (t) {
  case AtcType::ANY:
    return facility_is_endpoint(dep) && facility_is_endpoint(dest);
  case AtcType::TOWER_TOWER:
    return dep == FacilityType::TOWERED && dest == FacilityType::TOWERED;
  case AtcType::TOWER_AFIS:
    return dep == FacilityType::TOWERED && dest == FacilityType::AFIS;
  case AtcType::AFIS_AFIS:
    return dep == FacilityType::AFIS && dest == FacilityType::AFIS;
  case AtcType::AFIS_TOWER:
    return dep == FacilityType::AFIS && dest == FacilityType::TOWERED;
  }
  return false;
}

} // namespace

Country country_of(std::string_view icao) {
  if (icao.size() < 2)
    return Country::ANY;
  const std::string_view p = icao.substr(0, 2);
  if (p == "ED" || p == "ET")
    return Country::DE;
  if (p == "LS")
    return Country::CH;
  if (p == "LO")
    return Country::AT;
  return Country::ANY; // e.g. LZ — no dedicated selector
}

bool is_ga_suitable(const Airport &a) {
  if (a.icao.empty())
    return false;
  if (!facility_is_endpoint(a.facility))
    return false; // need Tower or AFIS
  for (const auto &r : a.runways) {
    if (r.length_m >= kMinRunwayM && surface_ga_usable(r.surface_code))
      return true;
  }
  return false;
}

int provisional_difficulty(const Airport &a) {
  // PLACEHOLDER heuristic — NOT the real LLM score (#5).
  int score = 1;

  // Controlled fields carry more RT workload than AFIS.
  if (a.facility == FacilityType::TOWERED)
    score += 3;
  else if (a.facility == FacilityType::AFIS)
    score += 1;

  // Elevation as a rough terrain/mountain proxy.
  if (a.elevation_ft >= 3000.0f)
    score += 2;
  else if (a.elevation_ft >= 1500.0f)
    score += 1;

  // Shortest runway: tighter strips are harder to operate.
  float shortest = 0.0f;
  for (const auto &r : a.runways) {
    if (shortest == 0.0f || r.length_m < shortest)
      shortest = r.length_m;
  }
  if (shortest > 0.0f && shortest < 600.0f)
    score += 2;
  else if (shortest > 0.0f && shortest < 1000.0f)
    score += 1;

  // Traffic proxy: many frequencies / runways suggest a busier field.
  if (a.frequencies.size() >= 6)
    score += 2;
  else if (a.frequencies.size() >= 4)
    score += 1;
  if (a.runways.size() >= 3)
    score += 1;

  return std::clamp(score, 1, 10);
}

ScoreResult default_provisional_score(const Airport &a) {
  return {provisional_difficulty(a), DifficultySource::PROVISIONAL_RULE};
}

Difficulty difficulty_bucket(int value) {
  if (value <= 3)
    return Difficulty::EASY;
  if (value <= 6)
    return Difficulty::MEDIUM;
  return Difficulty::HARD;
}

std::vector<Suggestion> suggest_flights(const std::vector<Airport> &airports,
                                        const Criteria &criteria,
                                        const ScoreFn &score,
                                        size_t max_results) {
  // 1. Prefilter to GA-suitable airports matching the country selector, and
  //    score each one once.
  struct Cand {
    const Airport *apt;
    ScoreResult score;
  };
  std::vector<Cand> cands;
  cands.reserve(airports.size());
  for (const auto &a : airports) {
    if (!is_ga_suitable(a))
      continue;
    if (criteria.country != Country::ANY &&
        country_of(a.icao) != criteria.country)
      continue;
    cands.push_back({&a, score(a)});
  }

  // Latitude window for a cheap bbox prefilter (1 deg lat ~ 111 km).
  const double lat_window =
      criteria.max_distance_km / 111.0 + 0.05;

  // 2. Build matching departure -> destination pairs.
  std::vector<Suggestion> out;
  for (const auto &dep : cands) {
    for (const auto &dest : cands) {
      if (dep.apt == dest.apt)
        continue;
      if (!atc_pair_ok(criteria.atc_type, dep.apt->facility,
                       dest.apt->facility))
        continue;
      // Difficulty applies to the destination (where you operate / land).
      if (criteria.difficulty != Difficulty::ANY &&
          difficulty_bucket(dest.score.value) != criteria.difficulty)
        continue;
      if (std::fabs(dep.apt->lat - dest.apt->lat) > lat_window)
        continue; // bbox guard
      const double km = airports::haversine_distance(
                            dep.apt->lat, dep.apt->lon, dest.apt->lat,
                            dest.apt->lon) /
                        1000.0;
      if (km <= kMinFlightKm || km > criteria.max_distance_km)
        continue;

      Suggestion s;
      s.dep_icao = dep.apt->icao;
      s.dep_name = dep.apt->name;
      s.dest_icao = dest.apt->icao;
      s.dest_name = dest.apt->name;
      s.distance_km = km;
      s.dep_facility = dep.apt->facility;
      s.dest_facility = dest.apt->facility;
      s.dep_difficulty = dep.score.value;
      s.dest_difficulty = dest.score.value;
      // Provenance follows the destination's score (the bucket-relevant one).
      s.difficulty_source = dest.score.source;
      out.push_back(std::move(s));
    }
  }

  // 3. Deterministic ranking: longer flights first (use the allowed radius),
  //    stable tie-break by ICAO pair.
  std::sort(out.begin(), out.end(), [](const Suggestion &a, const Suggestion &b) {
    if (a.distance_km != b.distance_km)
      return a.distance_km > b.distance_km;
    if (a.dep_icao != b.dep_icao)
      return a.dep_icao < b.dep_icao;
    return a.dest_icao < b.dest_icao;
  });

  // 4. Cap.
  if (out.size() > max_results)
    out.resize(max_results);
  return out;
}

} // namespace preflight
