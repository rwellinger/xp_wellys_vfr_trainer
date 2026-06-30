/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef AIRPORTS_AIRPORT_DB_HPP
#define AIRPORTS_AIRPORT_DB_HPP

#include "airports/airport_data.hpp"

#include <string>
#include <vector>

// In-memory DACH airport database. Parsing the ~1 GB apt.dat takes ~1.4 s, so
// the load runs on a single background worker (the main thread must never
// block). SDK-free: the file path is injected by the plugin layer, which
// resolves it via XPLMGetSystemPath. The worker is joined in stop(), satisfying
// the project rule that no worker outlives XPluginDisable/Stop.
namespace airports::airport_db {

// Start the background load (no-op if already started). Parses apt_dat_path,
// filters to DACH, and publishes the result.
void start_load(std::string apt_dat_path);

// True once the background load has finished (success or failure).
bool ready();

// The loaded DACH airports. Only valid once ready() is true; returns an empty
// vector before that (or if the file could not be opened).
const std::vector<Airport> &airports();

// Number of loaded airports (0 until ready).
size_t count();

// Join the worker thread and release the data. Safe to call multiple times.
void stop();

} // namespace airports::airport_db

#endif
