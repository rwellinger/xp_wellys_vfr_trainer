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
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace postflight::report_cache {

namespace {

// Suffix that identifies our own report files during a directory scan (keeps
// stray *.tmp files from an interrupted write out of the store).
constexpr const char *kFileSuffix = "_trainingsreport.json";

std::string dir_path;               // empty -> in-memory only
std::map<std::string, Entry> store; // session key -> entry

bool ends_with(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// UTC "YYYY-MM-DD_HHMM" for an epoch second (file-name timestamp).
std::string date_hhmm_utc(std::int64_t epoch) {
  std::time_t t = static_cast<std::time_t>(epoch);
  std::tm tm_utc{};
#if defined(_WIN32)
  gmtime_s(&tm_utc, &t); // MSVC: reversed arg order vs POSIX gmtime_r
#else
  gmtime_r(&t, &tm_utc);
#endif
  char buf[32] = {};
  std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H%M", &tm_utc);
  return buf;
}

json to_json(const Entry &e) {
  json findings = json::array();
  for (const auto &f : e.findings)
    findings.push_back(json{{"phase", f.phase},
                            {"category", f.category},
                            {"sentiment", f.sentiment},
                            {"text", f.text}});
  return json{
      {"version", kFileVersion},
      {"departure_icao", e.departure_icao},
      {"arrival_icao", e.arrival_icao},
      {"started_at_epoch", e.started_at_epoch},
      {"phraseology_score", e.phraseology_score},
      {"phraseology_rationale", e.phraseology_rationale},
      {"execution_score", e.execution_score},
      {"execution_rationale", e.execution_rationale},
      {"summary", e.summary},
      {"findings", std::move(findings)},
      {"language", e.language},
      {"model", e.model},
      {"computed_at", e.computed_at},
      {"prompt_version", e.prompt_version},
  };
}

Entry from_json(const json &d) {
  Entry e;
  e.departure_icao = d.value("departure_icao", std::string());
  e.arrival_icao = d.value("arrival_icao", std::string());
  e.started_at_epoch = d.value("started_at_epoch", std::int64_t{0});
  e.phraseology_score = d.value("phraseology_score", 0);
  e.phraseology_rationale = d.value("phraseology_rationale", std::string());
  e.execution_score = d.value("execution_score", 0);
  e.execution_rationale = d.value("execution_rationale", std::string());
  e.summary = d.value("summary", std::string());
  if (auto it = d.find("findings"); it != d.end() && it->is_array()) {
    for (const auto &f : *it) {
      if (!f.is_object())
        continue;
      Finding fd;
      fd.phase = f.value("phase", std::string());
      fd.category = f.value("category", std::string());
      fd.sentiment = f.value("sentiment", std::string());
      fd.text = f.value("text", std::string());
      e.findings.push_back(std::move(fd));
    }
  }
  e.language = d.value("language", std::string());
  e.model = d.value("model", std::string());
  e.computed_at = d.value("computed_at", std::string());
  e.prompt_version = d.value("prompt_version", 0);
  return e;
}

// Atomically write one report file (temp + rename).
void write_file(const Entry &e) {
  const std::string target =
      (fs::path(dir_path) / report_filename(e)).string();
  const std::string tmp = target + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out.good()) {
      logging::error("Failed to open %s for writing", tmp.c_str());
      return;
    }
    out << to_json(e).dump(2) << std::endl;
    if (!out.good()) {
      logging::error("Failed to write %s", tmp.c_str());
      return;
    }
  }
  if (std::rename(tmp.c_str(), target.c_str()) != 0)
    logging::error("Failed to rename %s -> %s", tmp.c_str(), target.c_str());
}

} // namespace

std::string make_key(const std::string &departure_icao,
                     std::int64_t started_at_epoch) {
  return departure_icao + "_" + std::to_string(started_at_epoch);
}

std::string report_filename(const Entry &e) {
  std::string route = e.departure_icao;
  if (!e.arrival_icao.empty())
    route += "-" + e.arrival_icao;
  return date_hhmm_utc(e.started_at_epoch) + "_" + route + kFileSuffix;
}

void load(const std::string &dir) {
  dir_path = dir;
  store.clear();

  if (dir.empty())
    return; // in-memory only

  std::error_code ec;
  fs::create_directories(dir, ec); // ignore "already exists"

  fs::directory_iterator it(dir, ec);
  if (ec)
    return; // directory missing / unreadable — start empty

  for (const auto &entry : it) {
    if (!entry.is_regular_file())
      continue;
    const std::string name = entry.path().filename().string();
    if (!ends_with(name, kFileSuffix))
      continue;

    std::ifstream in(entry.path());
    if (!in.good())
      continue;
    json doc;
    try {
      in >> doc;
    } catch (...) {
      logging::error("Failed to parse %s, skipping", name.c_str());
      continue;
    }
    if (!doc.is_object() || doc.value("version", 0) != kFileVersion)
      continue; // older/foreign layout — skip this file

    Entry e = from_json(doc);
    store.emplace(make_key(e.departure_icao, e.started_at_epoch), std::move(e));
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

void put(const std::string &key, Entry entry) { store[key] = std::move(entry); }

void save() {
  if (dir_path.empty())
    return;
  std::error_code ec;
  fs::create_directories(dir_path, ec);
  for (const auto &[key, e] : store) {
    (void)key;
    write_file(e);
  }
}

void clear() {
  store.clear();
  dir_path.clear();
}

} // namespace postflight::report_cache
