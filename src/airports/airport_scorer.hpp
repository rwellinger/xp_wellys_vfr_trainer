/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef AIRPORTS_AIRPORT_SCORER_HPP
#define AIRPORTS_AIRPORT_SCORER_HPP

#include "airports/airport_data.hpp"
#include "preflight/flight_suggester.hpp"

#include <string>

// Lazy LLM difficulty scoring on top of the score cache. The suggester reads
// scores synchronously via score_of() (cache hit -> LLM, else rule-based
// placeholder); enqueue_if_missing() + pump() compute missing ones in the
// background via backends::lm::respond_json_async. SDK-free (no XPLM); all
// state lives on the main thread.
namespace airports::scorer {

using airports::Airport;

// Scoring-prompt version. Bump when the prompt/rubric changes so cached scores
// from the old prompt are recomputed (see docs/airport_score_cache.md).
constexpr int kPromptVersion = 1;

// Stable signature over exactly the apt.dat fields the prompt consumes. Single
// source of truth for cache invalidation — must mirror build_user_prompt().
std::string input_sig(const Airport &a);

// ScoreFn for preflight::suggest_flights. Read-only: returns the cached LLM
// score (DifficultySource::LLM_SCORE) when present and current, otherwise the
// rule-based placeholder (PROVISIONAL_RULE). Never triggers an LLM call.
preflight::ScoreResult score_of(const Airport &a);

// Queue `a` for background scoring unless it is already cached, queued or in
// flight. Cheap and idempotent — call for every displayed destination.
void enqueue_if_missing(const Airport &a);

// Start the next queued scoring when nothing is in flight and a backend is
// ready. Call once per frame from the flight loop.
void pump();

// True while a destination is queued or being scored (UI hint).
bool busy();

// Number of airports still queued or in flight (for a "scoring N…" hint).
size_t pending_count();

// "provider:model" tag stored with each score for provenance (not used for
// invalidation). Set by the plugin when the backend (re)loads.
void set_model(const std::string &tag);

// Clear the queue and in-flight state (plugin disable / stop).
void stop();

} // namespace airports::scorer

#endif // AIRPORTS_AIRPORT_SCORER_HPP
