/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef AIRPORTS_AIRPORT_SCORE_CACHE_HPP
#define AIRPORTS_AIRPORT_SCORE_CACHE_HPP

#include <optional>
#include <string>

// Persistent cache of LLM-computed airport difficulty scores. One JSON file at
// <plugin>/data/airport_scores.json (decision + schema: docs/airport_score_cache.md).
// SDK-free: the path is injected by the plugin layer. All access is expected on
// the main thread (the scorer reads on draw and writes in the LLM callback,
// which the flight loop drains on the main thread), so there is no locking.
namespace airports::score_cache {

// Bump when the on-disk file layout changes incompatibly. A mismatch discards
// the whole file (per-entry prompt_version/input_sig handle finer invalidation).
constexpr int kFileVersion = 1;

struct Entry {
  int score = 0;            // 1-10
  int prompt_version = 0;   // scoring-prompt version that produced this
  std::string input_sig;    // signature over the scoring-relevant apt.dat inputs
  std::string model;        // "openai:gpt-4o-mini" — provenance only
  std::string computed_at;  // ISO-8601 UTC
  std::string rationale;    // optional short LLM justification (debug / UI)
};

// Set the file path and load it (no-op-safe on a missing file). Parse error or
// file-version mismatch -> start empty. Replaces any in-memory state.
void load(const std::string &path);

// Cached entry for `icao` iff it exists AND its prompt_version + input_sig match
// the arguments; otherwise nullptr (a miss -> recompute). The pointer is valid
// until the next put()/load()/clear().
const Entry *lookup_entry(const std::string &icao, int prompt_version,
                          const std::string &input_sig);

// Convenience: the cached score for a valid entry, else nullopt.
std::optional<int> lookup(const std::string &icao, int prompt_version,
                          const std::string &input_sig);

// Insert / replace the entry for `icao` (in memory only; call save() to persist).
void put(const std::string &icao, Entry entry);

// Atomically write the cache to the configured path (temp file + rename).
// No-op when no path was set (load() never called).
void save();

// Drop all in-memory state and the configured path (tests / shutdown).
void clear();

} // namespace airports::score_cache

#endif // AIRPORTS_AIRPORT_SCORE_CACHE_HPP
