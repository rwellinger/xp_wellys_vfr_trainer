/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef POSTFLIGHT_REPORT_CACHE_HPP
#define POSTFLIGHT_REPORT_CACHE_HPP

#include <cstdint>
#include <string>

// Persistent cache of LLM post-flight session reports. One JSON file at
// <plugin>/data/session_reports.json, keyed by "<departure_icao>_<started_at_epoch>"
// so re-opening a scored session shows the cached report without another LLM
// call. Mirrors airports::score_cache (SDK-free, main-thread access only, no
// locking, atomic temp+rename write).
namespace postflight::report_cache {

// Bump when the on-disk file layout changes incompatibly (discards the file).
constexpr int kFileVersion = 1;

struct Entry {
  std::string departure_icao;
  std::string arrival_icao;
  std::int64_t started_at_epoch = 0;
  int phraseology_score = 0; // 1-10
  std::string phraseology_rationale;
  int execution_score = 0; // 1-10
  std::string execution_rationale;
  std::string summary;
  std::string model;       // "openai:gpt-4o-mini" — provenance only
  std::string computed_at; // ISO-8601 UTC
  int prompt_version = 0;
};

// Stable session key from the ATC flight header.
std::string make_key(const std::string &departure_icao,
                     std::int64_t started_at_epoch);

// Set the file path and load it (no-op-safe on a missing file). Parse error or
// file-version mismatch -> start empty. Replaces any in-memory state.
void load(const std::string &path);

// Cached entry for `key`, or nullptr. Valid until the next put()/load()/clear().
const Entry *lookup(const std::string &key);

// Most-recent report by started_at_epoch, or nullptr when the cache is empty
// (UI shows the last flight's report on open).
const Entry *latest();

// Insert / replace (in memory only; call save() to persist).
void put(const std::string &key, Entry entry);

// Atomically write the cache to the configured path (temp file + rename).
void save();

// Drop all in-memory state and the configured path (tests / shutdown).
void clear();

} // namespace postflight::report_cache

#endif // POSTFLIGHT_REPORT_CACHE_HPP
