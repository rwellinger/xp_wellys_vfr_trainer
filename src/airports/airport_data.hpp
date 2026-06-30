/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef AIRPORTS_AIRPORT_DATA_HPP
#define AIRPORTS_AIRPORT_DATA_HPP

#include <cstdint>
#include <string>
#include <vector>

// SDK-free airport data model parsed from X-Plane apt.dat. Lives in the engine
// OBJECT library so it is unit-testable without the X-Plane SDK. The enums and
// frequency semantics are adapted from the ATC template's xplane_context (which
// solves the same apt.dat parsing problem) but reduced to what the trainer needs
// for airport selection and difficulty scoring.
namespace airports {

struct RunwayEnd {
  std::string number;       // e.g. "09", "27L"
  double lat = 0.0;
  double lon = 0.0;
  float heading_deg = 0.0f; // computed from both ends' lat/lon
};

struct Runway {
  RunwayEnd end1;
  RunwayEnd end2;
  float width_m = 0.0f;
  float length_m = 0.0f; // computed via haversine
  int surface_code = 0;  // 1=asphalt, 2=concrete, ...
};

// apt.dat encodes German AFIS/Info and Radio facilities under the Tower row
// code (1054); the only discriminator is the frequency name. INFO/RADIO are
// therefore refined from TOWER by name (see classify_by_name).
enum class FrequencyType {
  UNKNOWN,
  DELIVERY,
  GROUND,
  TOWER,
  UNICOM,
  CTAF,
  ATIS,
  INFO,  // German AFIS/Info facility
  RADIO, // Radio facility with Flugleiter
};

const char *frequency_type_name(FrequencyType ft);

// Operational class of an airport's radio service. AFIS (Info/Radio) is a
// first-class category, distinct from genuinely uncontrolled (UNICOM/CTAF).
enum class FacilityType {
  UNKNOWN = 0,      // no classifiable frequency
  UNCONTROLLED = 1, // UNICOM/CTAF self-announce field
  TOWERED = 2,      // controlled field with Tower
  AFIS = 3,         // Info/Radio facility: traffic info, no clearances
};

const char *facility_type_name(FacilityType ft);

struct Frequency {
  uint32_t freq_khz = 0; // e.g. 121900 for 121.900 MHz (exact integer)
  FrequencyType type = FrequencyType::UNKNOWN;

  float mhz() const { return static_cast<float>(freq_khz) / 1000.0f; }
};

struct Airport {
  std::string icao;
  std::string name;
  double lat = 0.0; // datum_lat (1302), runway-midpoint fallback
  double lon = 0.0; // datum_lon (1302), runway-midpoint fallback
  float elevation_ft = 0.0f;
  std::vector<Runway> runways;
  std::vector<Frequency> frequencies;
  FacilityType facility = FacilityType::UNKNOWN;

  bool has_frequency(FrequencyType ft) const {
    for (const auto &f : frequencies)
      if (f.type == ft)
        return true;
    return false;
  }
};

} // namespace airports

#endif
