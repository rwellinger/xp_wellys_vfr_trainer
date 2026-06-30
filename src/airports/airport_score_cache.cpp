/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "airports/airport_score_cache.hpp"

#include "core/logging.hpp"

#include <json.hpp>

#include <cstdio>
#include <fstream>
#include <map>

using json = nlohmann::json;

namespace airports::score_cache {

namespace {

std::string file_path;             // empty until load() is called
std::map<std::string, Entry> store; // ICAO -> entry

} // namespace

void load(const std::string &path) {
  file_path = path;
  store.clear();

  std::ifstream in(path);
  if (!in.good())
    return; // first run — start empty

  json doc;
  try {
    in >> doc;
  } catch (...) {
    logging::error("Failed to parse airport_scores.json, starting empty");
    return;
  }

  if (!doc.is_object() || doc.value("version", 0) != kFileVersion) {
    logging::info("airport_scores.json version mismatch, discarding");
    return;
  }

  const auto scores = doc.find("scores");
  if (scores == doc.end() || !scores->is_object())
    return;

  for (auto &[icao, e] : scores->items()) {
    if (!e.is_object())
      continue;
    Entry entry;
    entry.score = e.value("score", 0);
    entry.prompt_version = e.value("prompt_version", 0);
    entry.input_sig = e.value("input_sig", std::string());
    entry.model = e.value("model", std::string());
    entry.computed_at = e.value("computed_at", std::string());
    entry.rationale = e.value("rationale", std::string());
    store.emplace(icao, std::move(entry));
  }

  logging::info("Loaded %zu cached airport scores", store.size());
}

const Entry *lookup_entry(const std::string &icao, int prompt_version,
                          const std::string &input_sig) {
  auto it = store.find(icao);
  if (it == store.end())
    return nullptr;
  const Entry &e = it->second;
  if (e.prompt_version != prompt_version || e.input_sig != input_sig)
    return nullptr; // stale -> recompute
  return &e;
}

std::optional<int> lookup(const std::string &icao, int prompt_version,
                          const std::string &input_sig) {
  if (const Entry *e = lookup_entry(icao, prompt_version, input_sig))
    return e->score;
  return std::nullopt;
}

void put(const std::string &icao, Entry entry) {
  store[icao] = std::move(entry);
}

void save() {
  if (file_path.empty())
    return;

  json doc;
  doc["version"] = kFileVersion;
  json scores = json::object();
  for (const auto &[icao, e] : store) {
    scores[icao] = json{
        {"score", e.score},
        {"prompt_version", e.prompt_version},
        {"input_sig", e.input_sig},
        {"model", e.model},
        {"computed_at", e.computed_at},
        {"rationale", e.rationale},
    };
  }
  doc["scores"] = std::move(scores);

  // Atomic write: serialise to a temp file, then rename over the target so a
  // crash mid-write never leaves a truncated cache.
  const std::string tmp = file_path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out.good()) {
      logging::error("Failed to open %s for writing", tmp.c_str());
      return;
    }
    out << doc.dump(2) << std::endl;
    if (!out.good()) {
      logging::error("Failed to write %s", tmp.c_str());
      return;
    }
  }
  if (std::rename(tmp.c_str(), file_path.c_str()) != 0)
    logging::error("Failed to rename %s -> %s", tmp.c_str(), file_path.c_str());
}

void clear() {
  store.clear();
  file_path.clear();
}

} // namespace airports::score_cache
