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

// Does a destination's facility satisfy the requested filter?
bool dest_facility_ok(DestFacility want, FacilityType dest) {
  switch (want) {
  case DestFacility::ANY:
    return facility_is_endpoint(dest);
  case DestFacility::TOWER:
    return dest == FacilityType::TOWERED;
  case DestFacility::AFIS:
    return dest == FacilityType::AFIS;
  }
  return false;
}

} // namespace

const Airport *nearest_ga_airport(const std::vector<Airport> &airports,
                                  double lat, double lon) {
  const Airport *best = nullptr;
  double best_m = 0.0;
  for (const auto &a : airports) {
    if (!is_ga_suitable(a))
      continue;
    const double m = airports::haversine_distance(lat, lon, a.lat, a.lon);
    if (best == nullptr || m < best_m) {
      best = &a;
      best_m = m;
    }
  }
  return best;
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
  // The departure is the anchor (criteria.dep_lat/lon) supplied by the plugin
  // from the aircraft's current airport. The engine just lists reachable
  // destinations around it; the field at the anchor drops out via the
  // kMinFlightKm guard (distance ~0).

  // Latitude window for a cheap bbox prefilter (1 deg lat ~ 111 km).
  const double lat_window = criteria.max_distance_km / 111.0 + 0.05;

  std::vector<Suggestion> out;
  for (const auto &dest : airports) {
    if (!is_ga_suitable(dest))
      continue;
    if (!dest_facility_ok(criteria.dest_facility, dest.facility))
      continue;
    const ScoreResult dest_score = score(dest);
    // Difficulty applies to the destination (where you operate / land).
    if (criteria.difficulty != Difficulty::ANY &&
        difficulty_bucket(dest_score.value) != criteria.difficulty)
      continue;
    if (std::fabs(criteria.dep_lat - dest.lat) > lat_window)
      continue; // bbox guard
    const double km = airports::haversine_distance(criteria.dep_lat,
                                                   criteria.dep_lon, dest.lat,
                                                   dest.lon) /
                      1000.0;
    if (km <= kMinFlightKm || km > criteria.max_distance_km)
      continue;

    Suggestion s;
    s.dest_icao = dest.icao;
    s.dest_name = dest.name;
    s.distance_km = km;
    s.dest_facility = dest.facility;
    s.dest_difficulty = dest_score.value;
    // Provenance follows the destination's score (the bucket-relevant one).
    s.difficulty_source = dest_score.source;
    out.push_back(std::move(s));
  }

  // Deterministic ranking: nearest destinations first (the "what's reachable
  // around me" model), stable tie-break by destination ICAO.
  std::sort(out.begin(), out.end(), [](const Suggestion &a, const Suggestion &b) {
    if (a.distance_km != b.distance_km)
      return a.distance_km < b.distance_km;
    return a.dest_icao < b.dest_icao;
  });

  // 4. Cap.
  if (out.size() > max_results)
    out.resize(max_results);
  return out;
}

} // namespace preflight
