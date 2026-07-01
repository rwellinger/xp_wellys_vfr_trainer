/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "postflight/report_cache.hpp"

#include "core/logging.hpp"

#include <json.hpp>

#include <cstdio>
#include <fstream>
#include <map>

using json = nlohmann::json;

namespace postflight::report_cache {

namespace {

std::string file_path;              // empty until load() is called
std::map<std::string, Entry> store; // session key -> entry

} // namespace

std::string make_key(const std::string &departure_icao,
                     std::int64_t started_at_epoch) {
  return departure_icao + "_" + std::to_string(started_at_epoch);
}

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
    logging::error("Failed to parse session_reports.json, starting empty");
    return;
  }

  if (!doc.is_object() || doc.value("version", 0) != kFileVersion) {
    logging::info("session_reports.json version mismatch, discarding");
    return;
  }

  const auto sessions = doc.find("sessions");
  if (sessions == doc.end() || !sessions->is_object())
    return;

  for (auto &[key, e] : sessions->items()) {
    if (!e.is_object())
      continue;
    Entry entry;
    entry.departure_icao = e.value("departure_icao", std::string());
    entry.arrival_icao = e.value("arrival_icao", std::string());
    entry.started_at_epoch = e.value("started_at_epoch", std::int64_t{0});
    entry.phraseology_score = e.value("phraseology_score", 0);
    entry.phraseology_rationale = e.value("phraseology_rationale", std::string());
    entry.execution_score = e.value("execution_score", 0);
    entry.execution_rationale = e.value("execution_rationale", std::string());
    entry.summary = e.value("summary", std::string());
    entry.model = e.value("model", std::string());
    entry.computed_at = e.value("computed_at", std::string());
    entry.prompt_version = e.value("prompt_version", 0);
    store.emplace(key, std::move(entry));
  }

  logging::info("Loaded %zu cached session reports", store.size());
}

const Entry *lookup(const std::string &key) {
  auto it = store.find(key);
  return it == store.end() ? nullptr : &it->second;
}

const Entry *latest() {
  const Entry *best = nullptr;
  for (const auto &[key, e] : store) {
    (void)key;
    if (best == nullptr || e.started_at_epoch > best->started_at_epoch)
      best = &e;
  }
  return best;
}

void put(const std::string &key, Entry entry) {
  store[key] = std::move(entry);
}

void save() {
  if (file_path.empty())
    return;

  json doc;
  doc["version"] = kFileVersion;
  json sessions = json::object();
  for (const auto &[key, e] : store) {
    sessions[key] = json{
        {"departure_icao", e.departure_icao},
        {"arrival_icao", e.arrival_icao},
        {"started_at_epoch", e.started_at_epoch},
        {"phraseology_score", e.phraseology_score},
        {"phraseology_rationale", e.phraseology_rationale},
        {"execution_score", e.execution_score},
        {"execution_rationale", e.execution_rationale},
        {"summary", e.summary},
        {"model", e.model},
        {"computed_at", e.computed_at},
        {"prompt_version", e.prompt_version},
    };
  }
  doc["sessions"] = std::move(sessions);

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

} // namespace postflight::report_cache
