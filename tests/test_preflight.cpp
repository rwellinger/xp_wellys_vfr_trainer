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

bool has_pair(const std::vector<Suggestion> &v, const std::string &dep,
              const std::string &dest) {
  for (const auto &s : v)
    if (s.dep_icao == dep && s.dest_icao == dest)
      return true;
  return false;
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

TEST_CASE("country filter keeps only matching ICAO prefixes", "[preflight]") {
  Criteria c;
  c.country = Country::DE;
  c.max_distance_km = 100.0;
  auto v = suggest_flights(sample(), c);
  REQUIRE_FALSE(v.empty());
  for (const auto &s : v) {
    REQUIRE(country_of(s.dep_icao) == Country::DE);
    REQUIRE(country_of(s.dest_icao) == Country::DE);
  }
}

TEST_CASE("ATC type pairs departure and destination facility", "[preflight]") {
  Criteria c;
  c.country = Country::DE;
  c.atc_type = AtcType::TOWER_AFIS;
  c.max_distance_km = 100.0;
  auto v = suggest_flights(sample(), c);
  REQUIRE(v.size() == 1); // only EDAA(Tower) -> EDAB(AFIS) within 100 km
  REQUIRE(v[0].dep_icao == "EDAA");
  REQUIRE(v[0].dest_icao == "EDAB");
  REQUIRE(v[0].dep_facility == FacilityType::TOWERED);
  REQUIRE(v[0].dest_facility == FacilityType::AFIS);
}

TEST_CASE("distance cap excludes far pairs", "[preflight]") {
  Criteria c;
  c.max_distance_km = 60.0; // EDAA-EDAB ~55 km in; CH/AT pairs far out
  auto v = suggest_flights(sample(), c);
  REQUIRE_FALSE(v.empty());
  for (const auto &s : v)
    REQUIRE(s.distance_km <= 60.0);
  REQUIRE(has_pair(v, "EDAA", "EDAB"));
}

TEST_CASE("difficulty filter matches destination bucket", "[preflight]") {
  Criteria c;
  c.difficulty = Difficulty::EASY; // EDAB(3) and LOAA(2) are EASY
  c.max_distance_km = 1000.0;
  auto v = suggest_flights(sample(), c);
  REQUIRE_FALSE(v.empty());
  for (const auto &s : v)
    REQUIRE(difficulty_bucket(s.dest_difficulty) == Difficulty::EASY);
}

TEST_CASE("suggestions are deterministic and flagged provisional",
          "[preflight]") {
  Criteria c;
  c.max_distance_km = 1000.0;
  auto a = suggest_flights(sample(), c);
  auto b = suggest_flights(sample(), c);
  REQUIRE(a.size() == b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    REQUIRE(a[i].dep_icao == b[i].dep_icao);
    REQUIRE(a[i].dest_icao == b[i].dest_icao);
    REQUIRE(a[i].distance_km == Catch::Approx(b[i].distance_km));
    // Until #5, every difficulty value is a provisional placeholder.
    REQUIRE(a[i].difficulty_source == DifficultySource::PROVISIONAL_RULE);
  }
  // Ranked by descending distance.
  for (size_t i = 1; i < a.size(); ++i)
    REQUIRE(a[i - 1].distance_km >= a[i].distance_km);
}

TEST_CASE("no matches yields an empty result", "[preflight]") {
  Criteria c;
  c.country = Country::DE;
  c.atc_type = AtcType::TOWER_TOWER; // only one DE tower (EDAA) -> no pair
  c.max_distance_km = 1000.0;
  auto v = suggest_flights(sample(), c);
  REQUIRE(v.empty());
}

TEST_CASE("integration: real fixture yields EDDS -> EDFT for TOWER_AFIS",
          "[preflight]") {
  std::ifstream in(TESTDATA_DIR "/sample_apt.dat");
  REQUIRE(in.is_open());
  auto apts = airports::parse_apt_dat(in); // DACH: EDDS, EDFT, LSZH, LOWW

  Criteria c;
  c.country = Country::DE;          // EDDS, EDFT
  c.atc_type = AtcType::TOWER_AFIS; // EDDS tower -> EDFT afis
  c.max_distance_km = 250.0;        // EDDS-EDFT ~221 km
  auto v = suggest_flights(apts, c);
  REQUIRE(has_pair(v, "EDDS", "EDFT"));
  for (const auto &s : v) {
    REQUIRE(s.dep_facility == FacilityType::TOWERED);
    REQUIRE(s.dest_facility == FacilityType::AFIS);
  }
}
