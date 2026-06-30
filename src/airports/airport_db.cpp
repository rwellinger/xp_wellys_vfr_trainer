/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "airports/airport_db.hpp"

#include "airports/apt_dat_parser.hpp"
#include "core/logging.hpp"

#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>

namespace airports::airport_db {

namespace {

std::thread g_worker;
std::mutex g_mtx;                  // guards g_airports
std::vector<Airport> g_airports;  // published result
std::atomic<bool> g_ready{false}; // load finished (success or failure)
std::atomic<bool> g_started{false};

void load_worker(std::string path) {
  std::vector<Airport> parsed;
  std::ifstream in(path);
  if (!in.is_open()) {
    logging::error("airport_db: could not open %s", path.c_str());
  } else {
    parsed = parse_apt_dat(in); // default DACH filter
    logging::info("airport_db: loaded %zu DACH airports", parsed.size());
  }
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_airports = std::move(parsed);
  }
  g_ready.store(true);
}

} // namespace

void start_load(std::string apt_dat_path) {
  bool expected = false;
  if (!g_started.compare_exchange_strong(expected, true))
    return; // already started
  g_worker = std::thread(load_worker, std::move(apt_dat_path));
}

bool ready() { return g_ready.load(); }

const std::vector<Airport> &airports() {
  // Single writer publishes before flipping g_ready; readers only touch the
  // vector once ready() is true, so the reference is stable.
  return g_airports;
}

size_t count() {
  if (!g_ready.load())
    return 0;
  std::lock_guard<std::mutex> lk(g_mtx);
  return g_airports.size();
}

void stop() {
  if (g_worker.joinable())
    g_worker.join();
  std::lock_guard<std::mutex> lk(g_mtx);
  g_airports.clear();
  g_ready.store(false);
  g_started.store(false);
}

} // namespace airports::airport_db
