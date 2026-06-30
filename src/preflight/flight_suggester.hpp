/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef PREFLIGHT_FLIGHT_SUGGESTER_HPP
#define PREFLIGHT_FLIGHT_SUGGESTER_HPP

#include "airports/airport_data.hpp"

#include <functional>
#include <string>
#include <vector>

// Rule-based pre-flight suggestion engine (CONCEPT.md section 2). Given a
// departure anchor (the aircraft's current airport, resolved by the plugin
// layer from X-Plane's nav database) it lists reachable destinations within the
// requested radius. No LLM: the same inputs always produce the same output.
// SDK-free → the anchor position (lat/lon) is injected via Criteria, never read
// here. The engine does NOT pick the departure airport — that is the plugin's
// job (the nav DB is authoritative, unlike the apt.dat-derived list which can
// omit airports with sparse frequency data).
namespace preflight {

using airports::Airport;
using airports::FacilityType;

// Destination facility filter. The departure facility is fixed by the aircraft's
// position, so only the destination is selectable. AFIS covers Info/Radio
// fields; uncontrolled (UNICOM/CTAF) fields are never an endpoint.
enum class DestFacility { ANY, TOWER, AFIS };

// Difficulty bucket. Maps to a 1-10 score: EASY 1-3, MEDIUM 4-6, HARD 7-10.
enum class Difficulty { ANY, EASY, MEDIUM, HARD };

// Where a difficulty value came from. Until the LLM scoring (#5) exists, the
// engine always reports PROVISIONAL_RULE so a placeholder estimate is never
// mistaken for the real score.
enum class DifficultySource { PROVISIONAL_RULE, LLM_SCORE };

struct Criteria {
  // Current aircraft position; the departure is resolved to the nearest
  // GA-suitable field. Supplied by the plugin layer from the sim datarefs.
  double dep_lat = 0.0;
  double dep_lon = 0.0;
  DestFacility dest_facility = DestFacility::ANY;
  double max_distance_km = 150.0;
  Difficulty difficulty = Difficulty::ANY;
};

struct Suggestion {
  std::string dest_icao;
  std::string dest_name;
  double distance_km = 0.0; // from the departure anchor
  FacilityType dest_facility = FacilityType::UNKNOWN;
  int dest_difficulty = 0; // 1-10, the bucket-relevant value
  DifficultySource difficulty_source = DifficultySource::PROVISIONAL_RULE;
};

// A difficulty estimate plus its provenance.
struct ScoreResult {
  int value = 0; // 1-10
  DifficultySource source = DifficultySource::PROVISIONAL_RULE;
};

using ScoreFn = std::function<ScoreResult(const Airport &)>;

// Nearest GA-suitable airport to the given position, or nullptr when the list
// holds none. Provided as a fallback for resolving the departure when the nav
// database is unavailable; suggest_flights does not use it.
const Airport *nearest_ga_airport(const std::vector<Airport> &airports,
                                  double lat, double lon);

// GA-suitable: ICAO set, at least one runway of usable surface and a minimum
// length, and a Tower or AFIS facility (the dialog only deals in controlled /
// AFIS endpoints).
bool is_ga_suitable(const Airport &a);

// PLACEHOLDER difficulty heuristic — NOT the real LLM score (see issue #5).
// Returns a provisional 1-10 estimate from rule-based proxies (controlled
// field, elevation, runway length, traffic proxy). Always paired with
// DifficultySource::PROVISIONAL_RULE so callers can flag it as provisional.
int provisional_difficulty(const Airport &a);

// Default score source: provisional rule-based estimate. #5 will supply an
// alternative that returns cached LLM scores (DifficultySource::LLM_SCORE).
ScoreResult default_provisional_score(const Airport &a);

// Map a 1-10 difficulty value to its bucket.
Difficulty difficulty_bucket(int value);

// Produce ranked, deterministic flight suggestions. Empty when nothing matches.
std::vector<Suggestion>
suggest_flights(const std::vector<Airport> &airports, const Criteria &criteria,
                const ScoreFn &score = default_provisional_score,
                size_t max_results = 20);

} // namespace preflight

#endif
