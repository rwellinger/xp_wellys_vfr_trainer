/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef FMS_FMS_WRITER_HPP
#define FMS_FMS_WRITER_HPP

#include <string>

// Writes a minimal departure -> destination flight plan into the pilot's
// primary FMS flight plan via the XPLM410 multi-FPL API. This is the same
// injection approach the reference plugin xp_swiss_vfr uses (direct API write,
// not an .fms file on disk), reduced to two airport entries — no waypoints.
//
// XPLM-dependent: lives in the plugin module, not the SDK-free engine library.
namespace fms {

struct InjectResult {
  bool        ok = false;
  std::string message; // human-readable, surfaced in the UI (success or error)
};

// Number of entries currently in the pilot's primary flight plan. The UI uses
// this to decide whether to ask for confirmation before replacing an existing
// plan.
int primary_entry_count();

// Replace the primary flight plan with [dep_icao -> dest_icao] (two airport
// entries). The caller is responsible for confirming with the user when
// primary_entry_count() > 0 before calling this.
//
// Both ICAOs are resolved against the X-Plane navigation database. If either
// is not found, the flight plan is left untouched and ok=false is returned
// with a descriptive message — never a silent failure.
InjectResult inject_direct_plan(const std::string &dep_icao, int dep_elev_ft,
                                const std::string &dest_icao, int dest_elev_ft);

} // namespace fms

#endif
