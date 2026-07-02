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
#include <vector>

// Persistent store of LLM post-flight session reports. Since #21 each report is
// its own file under <X-Plane>/Output/xp_wellys_vfr_trainer/, named
// "<YYYY-MM-DD_HHMM>_<DEP>[-<DEST>]_trainingsreport.json" like the other
// plugins' logs. On load() the directory is scanned into an in-memory map keyed
// by "<departure_icao>_<started_at_epoch>", so a re-opened session shows the
// cached report without another LLM call. SDK-free (the directory is passed in
// from main.cpp), main-thread access only, no locking, atomic temp+rename write.
namespace postflight::report_cache {

// Bump when the per-file layout changes incompatibly (older files are skipped).
constexpr int kFileVersion = 2;

// One phase/call-linked detail note ("Detail-Teil"). Rendered only when present.
struct Finding {
  std::string phase;     // human-readable phase/call ref, e.g. "Initial call"
  std::string category;  // "phraseology" | "execution" | ""
  std::string sentiment; // "praise" | "critique"
  std::string text;      // one sentence
};

struct Entry {
  std::string departure_icao;
  std::string arrival_icao;
  std::int64_t started_at_epoch = 0;
  int phraseology_score = 0; // 1-10
  std::string phraseology_rationale;
  int execution_score = 0; // 1-10
  std::string execution_rationale;
  std::string summary;             // overall assessment ("Allgemein-Teil")
  std::vector<Finding> findings;   // phase-linked detail notes ("Detail-Teil")
  std::string language;            // "de" / "en" the report was generated in
  std::string model;               // "openai:gpt-4o-mini" — provenance only
  std::string computed_at;         // ISO-8601 UTC
  int prompt_version = 0;
};

// Stable session key from the ATC flight header.
std::string make_key(const std::string &departure_icao,
                     std::int64_t started_at_epoch);

// Per-flight file name for an entry (no directory), UTC-based:
// "<YYYY-MM-DD_HHMM>_<DEP>[-<DEST>]_trainingsreport.json".
std::string report_filename(const Entry &e);

// Set the directory and scan it for existing reports (created if missing;
// per-file parse error / version mismatch -> that file is skipped). An empty
// path keeps the store in memory only (save() becomes a no-op). Replaces any
// in-memory state.
void load(const std::string &dir);

// Cached entry for `key`, or nullptr. Valid until the next put()/load()/clear().
const Entry *lookup(const std::string &key);

// Most-recent report by started_at_epoch, or nullptr when the cache is empty
// (UI shows the last flight's report on open).
const Entry *latest();

// Insert / replace (in memory only; call save() to persist).
void put(const std::string &key, Entry entry);

// Write every in-memory entry to its own per-flight file in the configured
// directory (atomic temp file + rename each). No-op when no directory is set.
void save();

// Drop all in-memory state and the configured directory (tests / shutdown).
void clear();

} // namespace postflight::report_cache

#endif // POSTFLIGHT_REPORT_CACHE_HPP
