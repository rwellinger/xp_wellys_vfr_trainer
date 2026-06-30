/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include <catch2/catch_amalgamated.hpp>

#include "airports/airport_score_cache.hpp"
#include "airports/airport_scorer.hpp"
#include "preflight/flight_suggester.hpp"

using airports::Airport;
using airports::FacilityType;
using airports::FrequencyType;
using airports::Runway;
namespace cache = airports::score_cache;

namespace {

Airport mk(std::string icao, FacilityType fac, float elev, float rwy_len,
           int surface, int nfreq) {
  Airport a;
  a.icao = std::move(icao);
  a.name = "Test Field";
  a.lat = 48.0;
  a.lon = 9.0;
  a.elevation_ft = elev;
  a.facility = fac;
  Runway r;
  r.length_m = rwy_len;
  r.surface_code = surface;
  a.runways.push_back(r);
  for (int i = 0; i < nfreq; ++i)
    a.frequencies.push_back({120000u + static_cast<uint32_t>(i),
                             FrequencyType::TOWER});
  return a;
}

} // namespace

TEST_CASE("input_sig: stable, ignores name, reacts to scoring fields",
          "[scorer]") {
  Airport a = mk("EDDS", FacilityType::TOWERED, 1200.0f, 1500.0f, 1, 4);
  const std::string base = airports::scorer::input_sig(a);

  SECTION("name does not affect the signature") {
    Airport b = a;
    b.name = "Completely Different Name";
    b.lat = 47.5; // exact coords excluded too
    REQUIRE(airports::scorer::input_sig(b) == base);
  }
  SECTION("elevation changes it") {
    Airport b = a;
    b.elevation_ft += 1000.0f;
    REQUIRE(airports::scorer::input_sig(b) != base);
  }
  SECTION("facility changes it") {
    Airport b = a;
    b.facility = FacilityType::AFIS;
    REQUIRE(airports::scorer::input_sig(b) != base);
  }
  SECTION("an extra runway changes it") {
    Airport b = a;
    Runway r;
    r.length_m = 800.0f;
    r.surface_code = 3;
    b.runways.push_back(r);
    REQUIRE(airports::scorer::input_sig(b) != base);
  }
}

TEST_CASE("score_of: provisional without cache, LLM score with cache",
          "[scorer]") {
  cache::clear();
  Airport a = mk("EDDS", FacilityType::TOWERED, 1200.0f, 1500.0f, 1, 4);

  auto r1 = airports::scorer::score_of(a);
  REQUIRE(r1.source == preflight::DifficultySource::PROVISIONAL_RULE);
  REQUIRE(r1.value == preflight::provisional_difficulty(a));

  cache::Entry e;
  e.score = 9;
  e.prompt_version = airports::scorer::kPromptVersion;
  e.input_sig = airports::scorer::input_sig(a);
  cache::put(a.icao, e);

  auto r2 = airports::scorer::score_of(a);
  REQUIRE(r2.source == preflight::DifficultySource::LLM_SCORE);
  REQUIRE(r2.value == 9);

  cache::clear();
}
