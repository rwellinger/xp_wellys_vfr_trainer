/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "airports/apt_dat_parser.hpp"

#include "airports/geo.hpp"

#include <array>
#include <cmath>
#include <sstream>

namespace airports {

namespace {

// apt.dat frequency row codes -> FrequencyType. Old codes (50-55) carry the
// frequency as MHz*100; new codes (1050-1055) carry exact kHz.
struct FreqCode {
  int old_code;
  int new_code;
  FrequencyType type;
};
constexpr std::array<FreqCode, 5> kFreqCodes = {{
    {50, 1050, FrequencyType::ATIS},
    {51, 1051, FrequencyType::UNICOM},
    {52, 1052, FrequencyType::DELIVERY},
    {53, 1053, FrequencyType::GROUND},
    {54, 1054, FrequencyType::TOWER},
}};

std::string to_upper(std::string s) {
  for (char &c : s)
    if (c >= 'a' && c <= 'z')
      c = static_cast<char>(c - 'a' + 'A');
  return s;
}

// Finalise an airport: fill in the reference position (datum if present, else
// runway midpoint) and classify the facility from its frequencies.
void finalize(Airport &apt, bool has_datum, double mid_lat, double mid_lon,
              bool has_runway_mid) {
  if (!has_datum && has_runway_mid) {
    apt.lat = mid_lat;
    apt.lon = mid_lon;
  }
  apt.facility = classify_facility(apt.frequencies);
}

} // namespace

bool is_dach_airport(std::string_view icao) {
  if (icao.size() < 2)
    return false;
  const std::string_view p = icao.substr(0, 2);
  // DACH only: DE (ED civil + ET military), CH (LS), AT (LO). LZ (Slovakia) is
  // deliberately excluded.
  return p == "ED" || p == "ET" || p == "LS" || p == "LO";
}

int parse_line_code(const std::string &line) {
  size_t i = 0;
  while (i < line.size() && line[i] >= '0' && line[i] <= '9')
    ++i;
  if (i == 0 || i > 4)
    return -1;
  if (i < line.size() && line[i] != ' ' && line[i] != '\t')
    return -1;
  return std::stoi(line.substr(0, i));
}

FrequencyType classify_by_name(FrequencyType base, const std::string &name) {
  if (base != FrequencyType::TOWER)
    return base;

  size_t end = name.find_last_not_of(" \t\r\n");
  if (end == std::string::npos)
    return FrequencyType::TOWER; // no name -> Tower (legacy behaviour)
  size_t start = name.find_last_of(" \t\r\n", end);
  start = (start == std::string::npos) ? 0 : start + 1;
  const std::string last = to_upper(name.substr(start, end - start + 1));

  if (last == "INFO" || last == "INFORMATION" || last == "IFO")
    return FrequencyType::INFO;
  if (last == "RADIO" || last == "RDO")
    return FrequencyType::RADIO;
  return FrequencyType::TOWER;
}

FacilityType classify_facility(const std::vector<Frequency> &freqs) {
  auto has = [&](FrequencyType ft) {
    for (const auto &f : freqs)
      if (f.type == ft)
        return true;
    return false;
  };
  if (has(FrequencyType::TOWER))
    return FacilityType::TOWERED;
  if (has(FrequencyType::INFO) || has(FrequencyType::RADIO))
    return FacilityType::AFIS;
  if (has(FrequencyType::UNICOM) || has(FrequencyType::CTAF))
    return FacilityType::UNCONTROLLED;
  return FacilityType::UNKNOWN;
}

std::vector<Airport>
parse_apt_dat(std::istream &in,
              const std::function<bool(std::string_view)> &accept) {
  std::vector<Airport> result;

  Airport current;          // airport currently being assembled
  bool building = false;    // a header was seen and is a DACH candidate
  bool has_datum = false;   // 1302 datum_lat/lon seen for current
  double mid_lat_sum = 0.0; // first runway midpoint (position fallback)
  double mid_lon_sum = 0.0;
  bool has_runway_mid = false;
  int header_code = 0;      // header row code: 1=land, 16=seaplane, 17=heliport
  std::string header_id;    // apt.dat row-1 identifier (may be a gateway code)

  // Cheap admission test on the row-1 identifier, before the real ICAO is
  // known. Most airports use their ICAO as the row-1 id, but the X-Plane
  // Gateway assigns provisional ids ('X' + region + sequence, e.g. XEDF0,
  // XLS0013) whose real ICAO only appears later in 1302 icao_code. Admit those
  // by stripping the leading 'X'; the final accept() on the canonical code
  // decides for real at flush time.
  auto dach_candidate = [&](const std::string &id) {
    if (accept(id))
      return true;
    if (id.size() > 1 && id[0] == 'X')
      return accept(std::string_view(id).substr(1));
    return false;
  };

  auto flush = [&]() {
    if (building) {
      // Canonical ICAO: prefer 1302 icao_code (set during assembly), else fall
      // back to the row-1 id. icao_code is the key X-Plane's nav DB / FMS uses,
      // so e.g. Mollis resolves as LSZM, not its apt.dat id LSMF.
      if (current.icao.empty())
        current.icao = header_id;
      const bool is_heliport = header_code == 17;
      const bool is_closed = current.name.find("[X]") != std::string::npos;
      if (!is_heliport && !is_closed && accept(current.icao)) {
        finalize(current, has_datum, mid_lat_sum, mid_lon_sum, has_runway_mid);
        result.push_back(std::move(current));
      }
    }
    current = Airport{};
    building = false;
    has_datum = false;
    has_runway_mid = false;
    header_code = 0;
    header_id.clear();
  };

  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      continue;

    const int code = parse_line_code(line);

    // Airport header: 1 (land), 16 (seaplane), 17 (heliport). Closes the
    // previous airport and opens a new one. Heliports and [X]-closed fields are
    // assembled but dropped at flush(); the DACH/icao filter is also deferred to
    // flush() so gateway-coded fields (real ICAO in 1302 icao_code) survive.
    if (code == 1 || code == 16 || code == 17) {
      flush();

      // Tokens: 0=code, 1=elevation_ft, 2=deprecated, 3=deprecated, 4=id,
      // 5+=name.
      std::istringstream iss(line);
      std::string code_tok, elev_tok, dep1, dep2, id;
      iss >> code_tok >> elev_tok >> dep1 >> dep2 >> id;
      if (id.empty() || !dach_candidate(id))
        continue; // skip this airport's child records until the next header

      building = true;
      header_code = code;
      header_id = id;
      // current.icao stays empty until 1302 icao_code; flush() falls back to id.
      try {
        current.elevation_ft = std::stof(elev_tok);
      } catch (...) {
        current.elevation_ft = 0.0f;
      }
      // Remainder of the line is the airport name.
      std::string rest;
      std::getline(iss, rest);
      size_t s = rest.find_first_not_of(" \t");
      size_t e = rest.find_last_not_of(" \t\r");
      if (s != std::string::npos)
        current.name = rest.substr(s, e - s + 1);
      continue;
    }

    if (!building)
      continue;

    // Metadata: 1302 key value. We care about datum_lat / datum_lon.
    if (code == 1302) {
      std::istringstream iss(line);
      std::string code_tok, key, value;
      iss >> code_tok >> key >> value;
      try {
        if (key == "datum_lat") {
          current.lat = std::stod(value);
          has_datum = true; // lat first; lon expected on its own 1302 line
        } else if (key == "datum_lon") {
          current.lon = std::stod(value);
          has_datum = true;
        } else if (key == "icao_code" && !value.empty()) {
          // Real-world ICAO — overrides the row-1 id as the canonical code.
          current.icao = to_upper(value);
        }
      } catch (...) {
        // ignore malformed metadata
      }
      continue;
    }

    // Frequencies: 50-54 (old) / 1050-1054 (new).
    bool freq_handled = false;
    for (const auto &fc : kFreqCodes) {
      if (code == fc.old_code || code == fc.new_code) {
        std::istringstream iss(line);
        std::string code_tok, freq_tok;
        if (iss >> code_tok >> freq_tok) {
          try {
            uint32_t freq_int =
                static_cast<uint32_t>(std::stoul(freq_tok));
            uint32_t freq_khz =
                (code >= 1000) ? freq_int : freq_int * 10; // new=kHz, old=MHz*100
            std::string name_rest;
            std::getline(iss, name_rest);
            FrequencyType type = classify_by_name(fc.type, name_rest);
            current.frequencies.push_back({freq_khz, type});
          } catch (...) {
            // ignore malformed frequency
          }
        }
        freq_handled = true;
        break;
      }
    }
    if (freq_handled)
      continue;

    // Land runway: 100.
    if (code == 100) {
      std::istringstream iss(line);
      std::vector<std::string> t;
      std::string tok;
      while (iss >> tok)
        t.push_back(tok);
      if (t.size() < 20)
        continue;

      try {
        Runway rwy;
        rwy.width_m = std::stof(t[1]);
        rwy.surface_code = std::stoi(t[2]);
        rwy.end1.number = t[8];
        rwy.end1.lat = std::stod(t[9]);
        rwy.end1.lon = std::stod(t[10]);
        rwy.end2.number = t[17];
        rwy.end2.lat = std::stod(t[18]);
        rwy.end2.lon = std::stod(t[19]);
        rwy.length_m = static_cast<float>(haversine_distance(
            rwy.end1.lat, rwy.end1.lon, rwy.end2.lat, rwy.end2.lon));
        rwy.end1.heading_deg = initial_bearing(rwy.end1.lat, rwy.end1.lon,
                                               rwy.end2.lat, rwy.end2.lon);
        rwy.end2.heading_deg =
            std::fmod(rwy.end1.heading_deg + 180.0f, 360.0f);

        if (!has_runway_mid) {
          mid_lat_sum = (rwy.end1.lat + rwy.end2.lat) * 0.5;
          mid_lon_sum = (rwy.end1.lon + rwy.end2.lon) * 0.5;
          has_runway_mid = true;
        }
        current.runways.push_back(std::move(rwy));
      } catch (...) {
        // ignore malformed runway
      }
      continue;
    }
  }

  flush(); // emit the last airport (EOF acts as a header boundary)
  return result;
}

const char *frequency_type_name(FrequencyType ft) {
  switch (ft) {
  case FrequencyType::DELIVERY: return "DELIVERY";
  case FrequencyType::GROUND:   return "GROUND";
  case FrequencyType::TOWER:    return "TOWER";
  case FrequencyType::UNICOM:   return "UNICOM";
  case FrequencyType::CTAF:     return "CTAF";
  case FrequencyType::ATIS:     return "ATIS";
  case FrequencyType::INFO:     return "INFO";
  case FrequencyType::RADIO:    return "RADIO";
  case FrequencyType::UNKNOWN:  return "UNKNOWN";
  }
  return "UNKNOWN";
}

const char *facility_type_name(FacilityType ft) {
  switch (ft) {
  case FacilityType::UNCONTROLLED: return "UNCONTROLLED";
  case FacilityType::TOWERED:      return "TOWERED";
  case FacilityType::AFIS:         return "AFIS";
  case FacilityType::UNKNOWN:      return "UNKNOWN";
  }
  return "UNKNOWN";
}

} // namespace airports
