/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef AIRPORTS_GEO_HPP
#define AIRPORTS_GEO_HPP

// SDK-free great-circle helpers, ported from the ATC template's
// xplane_context_runtime.cpp. Used to derive runway length and headings from
// the two runway-end coordinates in apt.dat.
namespace airports {

// Great-circle distance in metres between two WGS84 points (degrees).
double haversine_distance(double lat1, double lon1, double lat2, double lon2);

// Initial great-circle bearing in degrees [0,360) from point 1 to point 2.
float initial_bearing(double lat1, double lon1, double lat2, double lon2);

} // namespace airports

#endif
