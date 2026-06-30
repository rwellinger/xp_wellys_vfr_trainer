/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include <catch2/catch_amalgamated.hpp>

#include "airports/apt_dat_parser.hpp"
#include "preflight/flight_suggester.hpp"

#include <fstream>

using namespace preflight;
using airports::Airport;
using airports::FacilityType;
using airports::Frequency;
using airports::FrequencyType;
using airports::Runway;

namespace {

// Build a synthetic airport. One runway of the given length/surface, `nfreq`
// dummy frequencies (only the count matters for the difficulty proxy).
Airport mk(std::string icao, std::string name, double lat, double lon,
           float elev, FacilityType fac, float rwy_len_m, int surface,
           int nfreq) {
  Airport a;
  a.icao = std::move(icao);
  a.name = std::move(name);
  a.lat = lat;
  a.lon = lon;
  a.elevation_ft = elev;
  a.facility = fac;
  if (rwy_len_m > 0.0f) {
    Runway r;
    r.length_m = rwy_len_m;
    r.surface_code = surface;
    a.runways.push_back(r);
  }
  for (int i = 0; i < nfreq; ++i)
    a.frequencies.push_back({120000u + static_cast<uint32_t>(i), FrequencyType::TOWER});
  return a;
}

// Synthetic DACH set with a couple of GA-unsuitable decoys.
std::vector<Airport> sample() {
  return {
      mk("EDAA", "Alpha", 48.0, 9.0, 1000.0f, FacilityType::TOWERED, 1500.0f, 2, 4),
      mk("EDAB", "Bravo", 48.5, 9.0, 1200.0f, FacilityType::AFIS, 800.0f, 3, 1),
      mk("LSAA", "Charlie", 47.0, 8.0, 1400.0f, FacilityType::TOWERED, 2000.0f, 2, 6),
      mk("LOAA", "Delta", 48.1, 16.0, 600.0f, FacilityType::AFIS, 1200.0f, 2, 1),
      // Decoys — must be filtered out by is_ga_suitable:
      mk("EDWZ", "Water", 48.1, 9.1, 500.0f, FacilityType::TOWERED, 1500.0f, 7, 3),
      mk("EDSH", "Short", 48.2, 9.1, 500.0f, FacilityType::TOWERED, 300.0f, 2, 3),
      mk("EDUC", "Unctrl", 48.3, 9.1, 500.0f, FacilityType::UNCONTROLLED, 1500.0f, 2, 1),
  };
}

bool has_dest(const std::vector<Suggestion> &v, const std::string &dest) {
  for (const auto &s : v)
    if (s.dest_icao == dest)
      return true;
  return false;
}

const Airport *find_icao(const std::vector<Airport> &v, const std::string &icao) {
  for (const auto &a : v)
    if (a.icao == icao)
      return &a;
  return nullptr;
}

} // namespace

TEST_CASE("is_ga_suitable rejects unusable fields", "[preflight]") {
  auto v = sample();
  REQUIRE(is_ga_suitable(v[0]));        // EDAA tower, 1500m asphalt
  REQUIRE(is_ga_suitable(v[1]));        // EDAB afis, 800m grass
  REQUIRE_FALSE(is_ga_suitable(v[4]));  // water surface
  REQUIRE_FALSE(is_ga_suitable(v[5]));  // runway too short
  REQUIRE_FALSE(is_ga_suitable(v[6]));  // uncontrolled (no Tower/AFIS)
}

TEST_CASE("nearest_ga_airport picks the closest GA field, skipping decoys",
          "[preflight]") {
  auto v = sample();
  // Sitting exactly on EDAA -> EDAA is the departure.
  REQUIRE(nearest_ga_airport(v, 48.0, 9.0)->icao == "EDAA");
  // Sitting on the uncontrolled decoy EDUC (48.3, 9.1): it is not GA-suitable,
  // so the nearest *usable* field (EDAB at 48.5, 9.0) wins.
  REQUIRE(nearest_ga_airport(v, 48.3, 9.1)->icao == "EDAB");
  // A list with no GA-suitable field at all -> nullptr.
  std::vector<Airport> only_decoys = {
      mk("EDUC", "Unctrl", 48.3, 9.1, 500.0f, FacilityType::UNCONTROLLED, 1500.0f, 2, 1),
  };
  REQUIRE(nearest_ga_airport(only_decoys, 48.0, 9.0) == nullptr);
}

TEST_CASE("the field at the anchor is never a destination", "[preflight]") {
  Criteria c;
  c.dep_lat = 48.0; // on EDAA
  c.dep_lon = 9.0;
  c.max_distance_km = 1000.0;
  auto v = suggest_flights(sample(), c);
  REQUIRE_FALSE(v.empty());
  for (const auto &s : v)
    REQUIRE(s.dest_icao != "EDAA"); // excluded by the kMinFlightKm guard
}

TEST_CASE("destinations lie within max_distance of the anchor", "[preflight]") {
  Criteria c;
  c.dep_lat = 48.0; // on EDAA
  c.dep_lon = 9.0;
  c.max_distance_km = 60.0; // EDAB ~55 km in; LSAA/LOAA far out
  auto v = suggest_flights(sample(), c);
  REQUIRE_FALSE(v.empty());
  for (const auto &s : v)
    REQUIRE(s.distance_km <= 60.0);
  REQUIRE(has_dest(v, "EDAB"));
}

TEST_CASE("dest_facility filter keeps only the requested facility", "[preflight]") {
  Criteria base;
  base.dep_lat = 48.0; // on EDAA (a Tower field)
  base.dep_lon = 9.0;
  base.max_distance_km = 1000.0;

  SECTION("AFIS only") {
    Criteria c = base;
    c.dest_facility = DestFacility::AFIS;
    auto v = suggest_flights(sample(), c);
    REQUIRE_FALSE(v.empty());
    for (const auto &s : v)
      REQUIRE(s.dest_facility == FacilityType::AFIS);
  }
  SECTION("Tower only") {
    Criteria c = base;
    c.dest_facility = DestFacility::TOWER;
    auto v = suggest_flights(sample(), c);
    REQUIRE_FALSE(v.empty());
    for (const auto &s : v)
      REQUIRE(s.dest_facility == FacilityType::TOWERED);
  }
}

TEST_CASE("candidates_in_range lists in-range GA fields, ignoring difficulty",
          "[preflight]") {
  Criteria c;
  c.dep_lat = 48.0; // on EDAA
  c.dep_lon = 9.0;
  c.max_distance_km = 60.0; // only EDAB (~55 km) is in range
  auto apts = sample(); // keep alive: candidates hold pointers into this vector
  auto cand = candidates_in_range(apts, c);
  bool has_edab = false, has_edaa = false;
  for (const auto &x : cand) {
    if (x.apt->icao == "EDAB")
      has_edab = true;
    if (x.apt->icao == "EDAA")
      has_edaa = true;
    REQUIRE(x.distance_km <= 60.0);
  }
  REQUIRE(has_edab);
  REQUIRE_FALSE(has_edaa); // the anchor field itself drops out
}

TEST_CASE("difficulty filter matches destination bucket (LLM-scored)",
          "[preflight]") {
  Criteria c;
  c.dep_lat = 48.0; // on EDAA
  c.dep_lon = 9.0;
  c.difficulty = Difficulty::EASY; // EDAB(3) and LOAA(2) are EASY
  c.max_distance_km = 1000.0;
  // Simulate a fully scored set: every airport carries a real LLM score.
  auto llm = [](const Airport &a) {
    return ScoreResult{provisional_difficulty(a), DifficultySource::LLM_SCORE};
  };
  auto v = suggest_flights(sample(), c, llm);
  REQUIRE_FALSE(v.empty());
  for (const auto &s : v) {
    REQUIRE(difficulty_bucket(s.dest_difficulty) == Difficulty::EASY);
    REQUIRE(s.difficulty_source == DifficultySource::LLM_SCORE);
  }
}

TEST_CASE("difficulty filter hides un-scored (provisional) airports",
          "[preflight]") {
  Criteria c;
  c.dep_lat = 48.0; // on EDAA
  c.dep_lon = 9.0;
  c.difficulty = Difficulty::EASY;
  c.max_distance_km = 1000.0;
  // Default ScoreFn yields provisional placeholders; a difficulty filter must
  // not trust those, so nothing shows until real scores arrive.
  auto v = suggest_flights(sample(), c);
  REQUIRE(v.empty());
}

TEST_CASE("suggestions are deterministic, provisional and nearest-first",
          "[preflight]") {
  Criteria c;
  c.dep_lat = 48.0; // on EDAA
  c.dep_lon = 9.0;
  c.max_distance_km = 1000.0;
  auto a = suggest_flights(sample(), c);
  auto b = suggest_flights(sample(), c);
  REQUIRE(a.size() == b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    REQUIRE(a[i].dest_icao == b[i].dest_icao);
    REQUIRE(a[i].distance_km == Catch::Approx(b[i].distance_km));
    // Until #5, every difficulty value is a provisional placeholder.
    REQUIRE(a[i].difficulty_source == DifficultySource::PROVISIONAL_RULE);
  }
  // Ranked by ascending distance (nearest destinations first).
  for (size_t i = 1; i < a.size(); ++i)
    REQUIRE(a[i - 1].distance_km <= a[i].distance_km);
}

TEST_CASE("no reachable destination yields an empty result", "[preflight]") {
  Criteria c;
  c.dep_lat = 48.0; // on EDAA — a departure exists ...
  c.dep_lon = 9.0;
  c.max_distance_km = 1.0; // ... but nothing is within 1 km
  auto v = suggest_flights(sample(), c);
  REQUIRE(v.empty());
}

TEST_CASE("integration: anchoring on EDDS reaches EDFT for an AFIS filter",
          "[preflight]") {
  std::ifstream in(TESTDATA_DIR "/sample_apt.dat");
  REQUIRE(in.is_open());
  auto apts = airports::parse_apt_dat(in); // DACH: EDDS, EDFT, LSZH, LOWW

  // Anchor on EDDS's reference position (as the plugin would after resolving
  // the current airport from the nav DB).
  const Airport *edds = find_icao(apts, "EDDS");
  REQUIRE(edds != nullptr);

  Criteria c;
  c.dep_lat = edds->lat;
  c.dep_lon = edds->lon;
  c.dest_facility = DestFacility::AFIS; // EDFT is the AFIS field
  c.max_distance_km = 250.0;            // EDDS-EDFT ~221 km
  auto v = suggest_flights(apts, c);
  REQUIRE(has_dest(v, "EDFT"));
  for (const auto &s : v)
    REQUIRE(s.dest_facility == FacilityType::AFIS);
}
