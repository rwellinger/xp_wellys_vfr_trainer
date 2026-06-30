/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "airports/airport_scorer.hpp"

#include "airports/airport_score_cache.hpp"
#include "backends/manager.hpp"
#include "core/logging.hpp"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <deque>
#include <set>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace airports::scorer {

namespace {

// ── Background queue state (main thread only) ────────────────────
std::deque<Airport> queue;       // airports awaiting a score (snapshots)
std::set<std::string> pending;   // ICAOs queued or in flight (dedup)
bool in_flight = false;          // one LLM call at a time
std::string model_tag;           // "openai:gpt-4o-mini" — provenance only

// Up to this many airports are scored in a single LLM call. Batching keeps the
// request count low (~1 call per 30 airfields) without risking the output-token
// limit or quality degradation that a single all-airports call would hit.
constexpr size_t kBatchSize = 30;

// ── System prompt (mirrored in docs/airport_difficulty_prompt.md) ─
const char *kSystemPrompt =
    "You are a VFR flight instructor familiar with general-aviation flying in "
    "the DACH region (Germany, Switzerland, Austria). For EACH airfield in the "
    "list, rate how demanding it is for a VFR pilot on a 1-10 scale: 1 = very "
    "easy (quiet AFIS/uncontrolled field, flat terrain, long runway), 10 = very "
    "demanding (busy controlled airspace and/or mountainous, high-elevation, "
    "short or tricky runway).\n"
    "Weigh: controlled (Tower) vs. AFIS workload; surrounding terrain and "
    "elevation (mountains, valley approaches); nearby controlled airspace "
    "(CTR/TMA) and traffic complexity; runway length, surface and count.\n"
    "Base the rating primarily on the facts provided. You may use general "
    "geographic knowledge of the SPECIFIC airfield named to judge terrain and "
    "airspace, but do NOT invent specific frequencies, procedures or figures; "
    "when unsure, stay conservative.\n"
    "Respond with ONLY a JSON object, no prose, with exactly one entry per "
    "airfield, keyed by its ICAO:\n"
    "{\"scores\": [{\"icao\": \"<ICAO>\", \"score\": <integer 1-10>, "
    "\"rationale\": \"<one short sentence>\"}]}";

uint64_t fnv1a(const std::string &s) {
  uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}

std::string now_iso() {
  std::time_t t = std::time(nullptr);
  std::tm tm_utc{};
  gmtime_r(&t, &tm_utc);
  char buf[32] = {};
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
  return buf;
}

// Human-readable facts block fed to the model. Keep in sync with input_sig().
std::string build_user_prompt(const Airport &a) {
  std::string out;
  out += "ICAO: " + a.icao + "\n";
  out += "Name: " + a.name + "\n";
  char pos[64];
  std::snprintf(pos, sizeof(pos), "Position: %.4f, %.4f\n", a.lat, a.lon);
  out += pos;
  char elev[48];
  std::snprintf(elev, sizeof(elev), "Elevation: %.0f ft\n",
                static_cast<double>(a.elevation_ft));
  out += elev;
  out += std::string("Facility: ") + facility_type_name(a.facility) + "\n";

  out += "Runways:\n";
  for (const auto &r : a.runways) {
    char line[96];
    std::snprintf(line, sizeof(line), "  - %.0f m, surface code %d\n",
                  static_cast<double>(r.length_m), r.surface_code);
    out += line;
  }
  out += "Frequencies:\n";
  for (const auto &f : a.frequencies) {
    char line[96];
    std::snprintf(line, sizeof(line), "  - %s %.3f MHz\n",
                  frequency_type_name(f.type), static_cast<double>(f.mhz()));
    out += line;
  }
  return out;
}

// Concatenate the per-airport fact blocks for one batch request.
std::string build_batch_prompt(const std::vector<Airport> &batch) {
  std::string out = "Airfields to rate:\n\n";
  for (const auto &a : batch) {
    out += build_user_prompt(a);
    out += "\n";
  }
  return out;
}

// Find an airport in the batch by ICAO (small N, linear is fine).
const Airport *find_in_batch(const std::vector<Airport> &batch,
                             const std::string &icao) {
  for (const auto &a : batch)
    if (a.icao == icao)
      return &a;
  return nullptr;
}

} // namespace

std::string input_sig(const Airport &a) {
  // Canonical string over exactly the scoring-relevant fields (NOT name/exact
  // coords, which do not change the rating for a given ICAO). Mirror any change
  // here in build_user_prompt() / kPromptVersion.
  std::string canon;
  canon += "f" + std::to_string(static_cast<int>(a.facility));

  canon += "|rc" + std::to_string(a.runways.size());
  double max_len = 0.0;
  std::vector<int> surfaces;
  surfaces.reserve(a.runways.size());
  for (const auto &r : a.runways) {
    max_len = std::max(max_len, static_cast<double>(r.length_m));
    surfaces.push_back(r.surface_code);
  }
  std::sort(surfaces.begin(), surfaces.end());
  canon += "|rl" + std::to_string(static_cast<long>(std::lround(max_len)));
  canon += "|sc";
  for (int s : surfaces)
    canon += std::to_string(s) + ",";

  std::vector<int> ftypes;
  ftypes.reserve(a.frequencies.size());
  for (const auto &f : a.frequencies)
    ftypes.push_back(static_cast<int>(f.type));
  std::sort(ftypes.begin(), ftypes.end());
  canon += "|fq";
  for (int t : ftypes)
    canon += std::to_string(t) + ",";

  canon += "|el" + std::to_string(static_cast<long>(std::lround(
                       static_cast<double>(a.elevation_ft))));

  char hex[17];
  std::snprintf(hex, sizeof(hex), "%016llx",
                static_cast<unsigned long long>(fnv1a(canon)));
  return hex;
}

preflight::ScoreResult score_of(const Airport &a) {
  const std::string sig = input_sig(a);
  if (auto s = score_cache::lookup(a.icao, kPromptVersion, sig))
    return {*s, preflight::DifficultySource::LLM_SCORE};
  return {preflight::provisional_difficulty(a),
          preflight::DifficultySource::PROVISIONAL_RULE};
}

void enqueue_if_missing(const Airport &a) {
  if (a.icao.empty())
    return;
  if (score_cache::lookup(a.icao, kPromptVersion, input_sig(a)))
    return; // already scored and current
  if (!pending.insert(a.icao).second)
    return; // already queued or in flight
  queue.push_back(a);
}

void pump() {
  if (in_flight || queue.empty())
    return;
  if (!backends::lm_ready())
    return; // no key/backend yet — keep the queue, retry next frame

  // Take up to kBatchSize airports for a single batched request.
  std::vector<Airport> batch;
  while (!queue.empty() && batch.size() < kBatchSize) {
    batch.push_back(std::move(queue.front()));
    queue.pop_front();
  }
  in_flight = true;
  const std::string model = model_tag;

  backends::lm::respond_json_async(
      kSystemPrompt, build_batch_prompt(batch),
      [batch, model](std::string text, bool success) {
        // Main thread (flight-loop drain).
        in_flight = false;

        // Whatever the model returns, this batch is no longer in flight; drop
        // every ICAO from `pending`. Un-returned ones stay uncached and get
        // re-enqueued on the next search.
        for (const auto &a : batch)
          pending.erase(a.icao);

        if (success) {
          try {
            json j = json::parse(text);
            const auto &arr = j.at("scores");
            int n = 0;
            for (const auto &item : arr) {
              const std::string icao = item.value("icao", std::string());
              const Airport *a = find_in_batch(batch, icao);
              if (!a)
                continue; // model returned an ICAO we did not ask for
              int score = std::max(1, std::min(10, item.at("score").get<int>()));
              score_cache::Entry e;
              e.score = score;
              e.prompt_version = kPromptVersion;
              e.input_sig = input_sig(*a);
              e.model = model;
              e.computed_at = now_iso();
              e.rationale = item.value("rationale", std::string());
              score_cache::put(icao, std::move(e));
              ++n;
            }
            if (n > 0)
              score_cache::save();
            logging::info("Scored %d/%zu airports in batch", n, batch.size());
          } catch (const std::exception &ex) {
            logging::error("Batch score parse failed: %s", ex.what());
          }
        } else {
          logging::error("Batch score request failed (%zu airports)",
                         batch.size());
        }

        pump(); // continue with the next batch
      });
}

bool busy() { return in_flight || !queue.empty(); }

size_t pending_count() { return pending.size(); }

void set_model(const std::string &tag) { model_tag = tag; }

void stop() {
  queue.clear();
  pending.clear();
  in_flight = false;
}

} // namespace airports::scorer
