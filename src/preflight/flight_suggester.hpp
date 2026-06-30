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
#include <string_view>
#include <vector>

// Rule-based pre-flight suggestion engine (CONCEPT.md section 2). Turns the
// user's criteria into concrete departure -> destination flight suggestions
// drawn from the DACH airport list. No LLM: the same inputs always produce the
// same output. SDK-free → unit-testable.
namespace preflight {

using airports::Airport;
using airports::FacilityType;

// Country selector. DE = ICAO prefixes ED/ET, CH = LS, AT = LO. ANY spans all
// DACH airports (also includes LZ, which has no dedicated selector).
enum class Country { ANY, DE, CH, AT };

// Departure-facility -> destination-facility pairing. AFIS covers Info/Radio
// fields; uncontrolled (UNICOM/CTAF) fields are never a Tower or AFIS endpoint.
enum class AtcType { ANY, TOWER_TOWER, TOWER_AFIS, AFIS_AFIS, AFIS_TOWER };

// Difficulty bucket. Maps to a 1-10 score: EASY 1-3, MEDIUM 4-6, HARD 7-10.
enum class Difficulty { ANY, EASY, MEDIUM, HARD };

// Where a difficulty value came from. Until the LLM scoring (#5) exists, the
// engine always reports PROVISIONAL_RULE so a placeholder estimate is never
// mistaken for the real score.
enum class DifficultySource { PROVISIONAL_RULE, LLM_SCORE };

struct Criteria {
  Country country = Country::ANY;
  AtcType atc_type = AtcType::ANY;
  double max_distance_km = 150.0;
  Difficulty difficulty = Difficulty::ANY;
};

struct Suggestion {
  std::string dep_icao;
  std::string dep_name;
  std::string dest_icao;
  std::string dest_name;
  double distance_km = 0.0;
  FacilityType dep_facility = FacilityType::UNKNOWN;
  FacilityType dest_facility = FacilityType::UNKNOWN;
  int dep_difficulty = 0;  // 1-10
  int dest_difficulty = 0; // 1-10, the bucket-relevant value
  DifficultySource difficulty_source = DifficultySource::PROVISIONAL_RULE;
};

// A difficulty estimate plus its provenance.
struct ScoreResult {
  int value = 0; // 1-10
  DifficultySource source = DifficultySource::PROVISIONAL_RULE;
};

using ScoreFn = std::function<ScoreResult(const Airport &)>;

// ICAO -> Country (ANY when not a recognised DACH prefix).
Country country_of(std::string_view icao);

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
