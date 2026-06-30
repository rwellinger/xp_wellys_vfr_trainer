/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "fms/fms_writer.hpp"

#include "core/logging.hpp"

#include <XPLM/XPLMNavigation.h>

namespace fms {
namespace {

// The pilot's primary flight plan — the slot the avionics (G1000 / GNS) expose
// to the pilot. The legacy single-FPL routines target a different, invisible
// slot, so we use the XPLM410 multi-FPL API throughout (see xp_swiss_vfr's
// procedure_runner for the same reasoning).
constexpr XPLMNavFlightPlan FPL = xplm_Fpl_Pilot_Primary;

} // namespace

int primary_entry_count() { return XPLMCountFMSFlightPlanEntries(FPL); }

InjectResult inject_direct_plan(const std::string &dep_icao, int dep_elev_ft,
                                const std::string &dest_icao, int dest_elev_ft) {
  // Resolve both airports up front so a lookup failure leaves the existing
  // flight plan untouched (no partial write, no silent fail).
  XPLMNavRef dep_ref =
      XPLMFindNavAid(nullptr, dep_icao.c_str(), nullptr, nullptr, nullptr,
                     xplm_Nav_Airport);
  if (dep_ref == XPLM_NAV_NOT_FOUND) {
    logging::error("FMS inject: departure %s not in nav data", dep_icao.c_str());
    return {false, "Departure " + dep_icao + " not in X-Plane nav data"};
  }

  XPLMNavRef dest_ref =
      XPLMFindNavAid(nullptr, dest_icao.c_str(), nullptr, nullptr, nullptr,
                     xplm_Nav_Airport);
  if (dest_ref == XPLM_NAV_NOT_FOUND) {
    logging::error("FMS inject: destination %s not in nav data",
                   dest_icao.c_str());
    return {false, "Destination " + dest_icao + " not in X-Plane nav data"};
  }

  // Replace the whole plan: clear back-to-front so indices stay valid as the
  // plan shrinks, then write the two airport entries at index 0 and 1. Airport
  // entries (not raw lat/lon) let the avionics recognise them as departure /
  // destination; the elevation is used as the VNAV reference altitude.
  for (int i = XPLMCountFMSFlightPlanEntries(FPL) - 1; i >= 0; --i)
    XPLMClearFMSFlightPlanEntry(FPL, i);

  XPLMSetFMSFlightPlanEntryInfo(FPL, 0, dep_ref, dep_elev_ft);
  XPLMSetFMSFlightPlanEntryInfo(FPL, 1, dest_ref, dest_elev_ft);

  // Make the destination (index 1) the active leg: the FMS flies from the
  // departure (n-1) toward the destination (n). Without this the avionics
  // default the active waypoint to the departure airport (index 0).
  XPLMSetDestinationFMSFlightPlanEntry(FPL, 1);

  logging::info("FMS inject: %s -> %s (%d entries)", dep_icao.c_str(),
                dest_icao.c_str(), XPLMCountFMSFlightPlanEntries(FPL));

  return {true, "Flight plan loaded: " + dep_icao + " -> " + dest_icao};
}

} // namespace fms
