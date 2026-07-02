/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef POSTFLIGHT_EVALUATOR_HPP
#define POSTFLIGHT_EVALUATOR_HPP

#include "postflight/atc_log.hpp"
#include "postflight/correlation.hpp"
#include "postflight/flight_log.hpp"

#include <string>

// Post-flight LLM judgement: builds a prompt from the correlated ATC/flight
// timeline and asks the backend for two 1-10 sub-scores (radio phraseology and
// flight execution) plus a short summary, via backends::lm::respond_json_async.
// The reply is parsed on the main thread (flight-loop drain), cached in
// report_cache and surfaced to the UI. Mirrors the airports::scorer pattern but
// is triggered on demand (one flight at a time) rather than queued — so there
// is no pump(); the existing backends::drain_callback_queue() delivers the
// reply. SDK-free; all state lives on the main thread.
namespace postflight::evaluator {

// Prompt/rubric version. Bump when the prompt changes so a re-open recomputes
// instead of showing a report from an older rubric.
constexpr int kPromptVersion = 2;

enum class Status { Idle, Running, Done, Error };

// Base system prompt (rubric + JSON contract), language-neutral. Documented in
// docs/post_flight_report.md.
extern const char *kSystemPrompt;

// Full system prompt: kSystemPrompt plus a language directive for the report
// text ("de" -> German, anything else -> English).
std::string system_prompt(const std::string &language);

// User prompt: compact correlated timeline + flight/landing summary.
std::string build_prompt(const CorrelatedTimeline &timeline,
                         const FlightLog &flight);

// Correlate + dispatch an async evaluation for one session. `language` ("de" /
// "en") selects the report language. Sets status to Running; the reply callback
// parses the JSON, clamps the scores to 1-10, stores the result
// (report_cache::put + save) and sets Done or Error. A no-op when an evaluation
// is already Running. The report_cache key the result is stored under is exposed
// via last_key().
void evaluate_async(const AtcLog &atc, const FlightLog &flight,
                    const std::string &language);

// Current evaluation status (UI hint).
Status status();

// Error message when status() == Error (empty otherwise).
const std::string &last_error();

// report_cache key of the most recent evaluation (empty until one starts).
const std::string &last_key();

// "provider:model" tag stored with each report for provenance. Set by the
// plugin when the backend (re)loads.
void set_model(const std::string &tag);

// Reset transient state (plugin disable / stop). Does not touch the cache.
void stop();

} // namespace postflight::evaluator

#endif // POSTFLIGHT_EVALUATOR_HPP
