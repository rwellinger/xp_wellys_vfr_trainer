/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include <catch2/catch_amalgamated.hpp>

#include "airports/apt_dat_parser.hpp"

#include <fstream>
#include <sstream>

using namespace airports;

namespace {

// Accept-all predicate so single-airport unit tests can parse non-DACH ICAOs.
bool accept_all(std::string_view) { return true; }

const Airport *find(const std::vector<Airport> &v, const std::string &icao) {
  for (const auto &a : v)
    if (a.icao == icao)
      return &a;
  return nullptr;
}

} // namespace

TEST_CASE("DACH filter accepts ED/ET/LS/LO and rejects others", "[airports]") {
  REQUIRE(is_dach_airport("EDDS"));  // Germany
  REQUIRE(is_dach_airport("ETNG"));  // Germany (military)
  REQUIRE(is_dach_airport("LSZH"));  // Switzerland
  REQUIRE(is_dach_airport("LOWW"));  // Austria
  REQUIRE_FALSE(is_dach_airport("LZIB")); // Slovakia — not DACH
  REQUIRE_FALSE(is_dach_airport("LFPG")); // France
  REQUIRE_FALSE(is_dach_airport("LEMD")); // Spain
  REQUIRE_FALSE(is_dach_airport("EGLL")); // UK
  REQUIRE_FALSE(is_dach_airport("E"));    // too short
}

TEST_CASE("parse_line_code reads leading codes", "[airports]") {
  REQUIRE(parse_line_code("1   1270 0 0 EDDS Stuttgart") == 1);
  REQUIRE(parse_line_code("1054 118805 Tower") == 1054);
  REQUIRE(parse_line_code("100 45.00 2 ...") == 100);
  REQUIRE(parse_line_code("") == -1);
  REQUIRE(parse_line_code("ABC") == -1);     // not digits
  REQUIRE(parse_line_code("12345 x") == -1); // > 4 digits
}

TEST_CASE("classify_by_name refines German AFIS/Info under the Tower code",
          "[airports]") {
  REQUIRE(classify_by_name(FrequencyType::TOWER, "Lauterbach Info") ==
          FrequencyType::INFO);
  REQUIRE(classify_by_name(FrequencyType::TOWER, "Egelsbach Information") ==
          FrequencyType::INFO);
  REQUIRE(classify_by_name(FrequencyType::TOWER, "Some Radio") ==
          FrequencyType::RADIO);
  REQUIRE(classify_by_name(FrequencyType::TOWER, "Stuttgart Tower") ==
          FrequencyType::TOWER);
  // Non-Tower base types pass through unchanged.
  REQUIRE(classify_by_name(FrequencyType::GROUND, "X Info") ==
          FrequencyType::GROUND);
}

TEST_CASE("header parse yields icao, name and elevation", "[airports]") {
  std::istringstream in("1   1270 0 0 EDDS Stuttgart\n");
  auto v = parse_apt_dat(in, accept_all);
  REQUIRE(v.size() == 1);
  REQUIRE(v[0].icao == "EDDS");
  REQUIRE(v[0].name == "Stuttgart");
  REQUIRE(v[0].elevation_ft == Catch::Approx(1270.0f));
}

TEST_CASE("1302 icao_code overrides the row-1 id as canonical ICAO",
          "[airports]") {
  // Mollis: apt.dat row-1 id is LSMF, but the real ICAO (the key X-Plane's nav
  // DB / FMS uses) is LSZM.
  std::istringstream in("1 1482 0 0 LSMF Mollis\n"
                        "1302 icao_code LSZM\n");
  auto v = parse_apt_dat(in, accept_all);
  REQUIRE(v.size() == 1);
  REQUIRE(v[0].icao == "LSZM");
}

TEST_CASE("gateway placeholder id is admitted; real ICAO comes from icao_code",
          "[airports]") {
  // X-Plane Gateway ids like XEDF0 do not match the DACH prefix on their own,
  // but the field is a DACH airport (EDVD) and must survive the filter.
  std::istringstream in("1 500 0 0 XEDF0 Uslar\n"
                        "1302 icao_code EDVD\n");
  auto v = parse_apt_dat(in, is_dach_airport);
  REQUIRE(v.size() == 1);
  REQUIRE(v[0].icao == "EDVD");
}

TEST_CASE("closed [X] airports are filtered out", "[airports]") {
  std::istringstream in("1 167 0 0 LSMI [X] Interlaken\n"
                        "1302 icao_code LSMI\n"
                        "1 1270 0 0 EDDS Stuttgart\n");
  auto v = parse_apt_dat(in, accept_all);
  REQUIRE(v.size() == 1);
  REQUIRE(v[0].icao == "EDDS"); // the closed field is dropped
}

TEST_CASE("heliports (row code 17) are filtered out", "[airports]") {
  std::istringstream in("17 1340 0 0 XLS000W Universitatsspital Zurich\n"
                        "1302 icao_code LSHZ\n"
                        "1 1270 0 0 EDDS Stuttgart\n");
  auto v = parse_apt_dat(in, accept_all);
  REQUIRE(v.size() == 1);
  REQUIRE(v[0].icao == "EDDS"); // the heliport is dropped
}

TEST_CASE("runway parse computes length and reciprocal headings",
          "[airports]") {
  std::istringstream in(
      "1 1270 0 0 EDDS Stuttgart\n"
      "100 45.00 2 802 0.25 1 3 0  07 48.6857316 9.2001310 300 0 7 2 1 2 "
      "25 48.6940159 9.2438095 0 0 7 2 1 2\n");
  auto v = parse_apt_dat(in, accept_all);
  REQUIRE(v.size() == 1);
  REQUIRE(v[0].runways.size() == 1);
  const Runway &r = v[0].runways[0];
  REQUIRE(r.width_m == Catch::Approx(45.0f));
  REQUIRE(r.surface_code == 2);
  REQUIRE(r.end1.number == "07");
  REQUIRE(r.end2.number == "25");
  // EDDS 07/25 is ~3340 m.
  REQUIRE(r.length_m == Catch::Approx(3340.0f).margin(60.0f));
  // Reciprocal ends differ by 180 deg.
  float diff = std::fabs(r.end1.heading_deg - r.end2.heading_deg);
  REQUIRE(diff == Catch::Approx(180.0f).margin(0.5f));
}

TEST_CASE("frequency parse handles new (kHz) and old (MHz*100) formats",
          "[airports]") {
  SECTION("new format 1050-1054 is already kHz") {
    std::istringstream in("1 0 0 0 EDXX Test\n"
                          "1054 118805 Test Tower\n");
    auto v = parse_apt_dat(in, accept_all);
    REQUIRE(v[0].frequencies.size() == 1);
    REQUIRE(v[0].frequencies[0].freq_khz == 118805u);
    REQUIRE(v[0].frequencies[0].mhz() == Catch::Approx(118.805f));
    REQUIRE(v[0].frequencies[0].type == FrequencyType::TOWER);
  }
  SECTION("old format 50-54 is MHz*100, scaled by 10 to kHz") {
    std::istringstream in("1 0 0 0 EDXX Test\n"
                          "53 12190 Test Ground\n");
    auto v = parse_apt_dat(in, accept_all);
    REQUIRE(v[0].frequencies.size() == 1);
    REQUIRE(v[0].frequencies[0].freq_khz == 121900u);
    REQUIRE(v[0].frequencies[0].type == FrequencyType::GROUND);
  }
}

TEST_CASE("full fixture parse filters to DACH only", "[airports]") {
  std::ifstream in(TESTDATA_DIR "/sample_apt.dat");
  REQUIRE(in.is_open());
  auto v = parse_apt_dat(in); // default DACH filter

  // EDDS, EDFT, LSZH, LOWW kept; LFPG (France) dropped.
  REQUIRE(v.size() == 4);
  REQUIRE(find(v, "LFPG") == nullptr);

  const Airport *edds = find(v, "EDDS");
  REQUIRE(edds != nullptr);
  REQUIRE(edds->name == "Stuttgart");
  REQUIRE(edds->elevation_ft == Catch::Approx(1270.0f));
  REQUIRE(edds->lat == Catch::Approx(48.689877778).margin(1e-6));
  REQUIRE(edds->lon == Catch::Approx(9.221963889).margin(1e-6));
  REQUIRE(edds->runways.size() == 1);
  REQUIRE(edds->facility == FacilityType::TOWERED);
  REQUIRE(edds->has_frequency(FrequencyType::GROUND));

  // EDFT files its info frequency under the Tower code -> AFIS facility.
  const Airport *edft = find(v, "EDFT");
  REQUIRE(edft != nullptr);
  REQUIRE(edft->facility == FacilityType::AFIS);
  REQUIRE(edft->has_frequency(FrequencyType::INFO));

  const Airport *lszh = find(v, "LSZH");
  REQUIRE(lszh != nullptr);
  REQUIRE(lszh->runways.size() == 3);
  REQUIRE(lszh->facility == FacilityType::TOWERED);
}
