/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "postflight/evaluator.hpp"

#include "backends/manager.hpp"
#include "core/logging.hpp"
#include "postflight/report_cache.hpp"

#include <json.hpp>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

using json = nlohmann::json;

namespace postflight::evaluator {

const char *kSystemPrompt =
    "You are a VFR flight instructor in the DACH region (Germany, Switzerland, "
    "Austria) debriefing a student after a single flight. You receive a "
    "time-correlated timeline of the pilot's radio calls (with the "
    "controller's replies) joined to the aircraft's flight track, plus a "
    "landing summary.\n"
    "Rate the flight on two independent 1-10 scales (1 = poor, 10 = "
    "exemplary):\n"
    "- phraseology_score: radio discipline and standard VFR phraseology — "
    "correct initial calls, position reports, readbacks, callsign use, "
    "no missing or garbled elements.\n"
    "- execution_score: flight execution — sensible altitudes and pattern "
    "flying, stable approach and the landing quality provided.\n"
    "Judge only from the data given; do NOT invent facts, frequencies or "
    "events. Keep each rationale to one or two sentences; the summary to two "
    "or three sentences of actionable debrief.\n"
    "Respond with ONLY a JSON object, no prose:\n"
    "{\"phraseology_score\": <integer 1-10>, \"phraseology_rationale\": "
    "\"<text>\", \"execution_score\": <integer 1-10>, "
    "\"execution_rationale\": \"<text>\", \"summary\": \"<text>\"}";

namespace {

// ── State (main thread only) ─────────────────────────────────────
Status g_status = Status::Idle;
std::string g_error;
std::string g_key;
std::string g_model; // "openai:gpt-4o-mini" — provenance only

std::string now_iso() {
  std::time_t t = std::time(nullptr);
  std::tm tm_utc{};
  gmtime_r(&t, &tm_utc);
  char buf[32] = {};
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return buf;
}

// UTC HH:MM:SS for an epoch second (readable timeline labels).
std::string hms_utc(std::int64_t ts) {
  std::time_t t = static_cast<std::time_t>(ts);
  std::tm tm_utc{};
  gmtime_r(&t, &tm_utc);
  char buf[16] = {};
  std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_utc);
  return buf;
}

int clamp_score(int v) { return std::max(1, std::min(10, v)); }

} // namespace

std::string build_prompt(const CorrelatedTimeline &timeline,
                         const FlightLog &flight) {
  std::string out;

  // ── Flight header ──
  out += "Flight: " + flight.departure_icao + " -> " + flight.arrival_icao;
  if (!flight.aircraft_icao.empty())
    out += " (" + flight.aircraft_icao + ")";
  out += "\n";
  if (flight.block_time_min > 0)
    out += "Block time: " + std::to_string(flight.block_time_min) + " min\n";
  if (flight.max_altitude_ft > 0)
    out += "Max altitude: " + std::to_string(flight.max_altitude_ft) + " ft\n";

  // ── Radio + track timeline ──
  out += "\nTimeline (UTC time | phase | pilot -> controller | position):\n";
  for (const auto &ev : timeline.events) {
    const auto &tx = ev.tx;
    out += "[" + hms_utc(tx.ts) + " " + tx.flight_phase + "] ";
    out += "\"" + tx.transcript + "\"";
    if (!tx.intent.empty())
      out += " (" + tx.intent + ")";
    if (!tx.atc_response.empty())
      out += " -> ATC: \"" + tx.atc_response + "\"";
    if (tx.readback_issue)
      out += " [readback incomplete]";
    if (ev.has_position) {
      char pos[64];
      std::snprintf(pos, sizeof(pos), " @ %d ft, %d kt IAS", ev.track.alt_ft,
                    ev.track.spd_kts);
      out += pos;
    }
    out += "\n";
  }

  // ── Landing summary ──
  if (!flight.landings.empty()) {
    out += "\nLandings:\n";
    for (const auto &l : flight.landings) {
      char line[192];
      std::snprintf(line, sizeof(line),
                    "  - rating %s, %.0f fpm, %.2f G, bounces %d, flare \"%s\", "
                    "crosswind %d kt (%s)\n",
                    l.rating.c_str(), l.fpm, l.g_force, l.bounce_count,
                    l.flare.c_str(), l.crosswind_kts, l.wind_status.c_str());
      out += line;
    }
  }

  return out;
}

void evaluate_async(const AtcLog &atc, const FlightLog &flight) {
  if (g_status == Status::Running)
    return; // one evaluation at a time

  g_key = report_cache::make_key(atc.flight.departure_airport,
                                 atc.flight.started_at_epoch);
  g_error.clear();

  if (!backends::lm_ready()) {
    g_status = Status::Error;
    g_error = "No LLM backend ready (set an API key in Settings)";
    return;
  }

  const CorrelatedTimeline timeline = correlate(atc, flight);
  const std::string user_prompt = build_prompt(timeline, flight);

  g_status = Status::Running;

  // Snapshot the values the callback needs (it runs on a later frame).
  const std::string key = g_key;
  const std::string model = g_model;
  const std::string dep = atc.flight.departure_airport;
  const std::string arr = flight.arrival_icao;
  const std::int64_t started = atc.flight.started_at_epoch;

  backends::lm::respond_json_async(
      kSystemPrompt, user_prompt,
      [key, model, dep, arr, started](std::string text, bool success) {
        // Main thread (flight-loop drain).
        if (!success) {
          g_status = Status::Error;
          g_error = "LLM request failed";
          logging::error("Post-flight evaluation request failed");
          return;
        }
        try {
          json j = json::parse(text);
          report_cache::Entry e;
          e.departure_icao = dep;
          e.arrival_icao = arr;
          e.started_at_epoch = started;
          e.phraseology_score = clamp_score(j.at("phraseology_score").get<int>());
          e.phraseology_rationale =
              j.value("phraseology_rationale", std::string());
          e.execution_score = clamp_score(j.at("execution_score").get<int>());
          e.execution_rationale =
              j.value("execution_rationale", std::string());
          e.summary = j.value("summary", std::string());
          e.model = model;
          e.computed_at = now_iso();
          e.prompt_version = kPromptVersion;
          report_cache::put(key, std::move(e));
          report_cache::save();
          g_status = Status::Done;
          logging::info("Post-flight evaluation stored for %s", key.c_str());
        } catch (const std::exception &ex) {
          g_status = Status::Error;
          g_error = std::string("Could not parse LLM reply: ") + ex.what();
          logging::error("Post-flight evaluation parse failed: %s", ex.what());
        }
      });
}

Status status() { return g_status; }

const std::string &last_error() { return g_error; }

const std::string &last_key() { return g_key; }

void set_model(const std::string &tag) { g_model = tag; }

void stop() {
  g_status = Status::Idle;
  g_error.clear();
  g_key.clear();
}

} // namespace postflight::evaluator
