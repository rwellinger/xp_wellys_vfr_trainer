/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include <catch2/catch_amalgamated.hpp>

#include "airports/airport_score_cache.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using airports::score_cache::Entry;
namespace cache = airports::score_cache;

namespace {
std::string tmp_path(const std::string &name) {
  return (fs::temp_directory_path() / name).string();
}
} // namespace

TEST_CASE("score cache: put/save/load roundtrip", "[score_cache]") {
  const std::string path = tmp_path("xpvt_scores_roundtrip.json");
  fs::remove(path);
  cache::clear();
  cache::load(path); // missing file -> empty
  REQUIRE_FALSE(cache::lookup("EDDS", 1, "sig1").has_value());

  Entry e;
  e.score = 7;
  e.prompt_version = 1;
  e.input_sig = "sig1";
  e.model = "openai:gpt-4o-mini";
  e.computed_at = "2026-06-30T00:00:00Z";
  e.rationale = "busy class C";
  cache::put("EDDS", e);
  cache::save();

  // Reload from disk into a fresh state.
  cache::clear();
  cache::load(path);
  auto s = cache::lookup("EDDS", 1, "sig1");
  REQUIRE(s.has_value());
  REQUIRE(*s == 7);
  const Entry *entry = cache::lookup_entry("EDDS", 1, "sig1");
  REQUIRE(entry != nullptr);
  REQUIRE(entry->rationale == "busy class C");
  REQUIRE(entry->model == "openai:gpt-4o-mini");

  fs::remove(path);
  cache::clear();
}

TEST_CASE("score cache: invalidation on prompt_version / input_sig",
          "[score_cache]") {
  cache::clear();
  Entry e;
  e.score = 5;
  e.prompt_version = 2;
  e.input_sig = "abc";
  cache::put("EDFT", e);

  REQUIRE(cache::lookup("EDFT", 2, "abc").has_value());       // hit
  REQUIRE_FALSE(cache::lookup("EDFT", 3, "abc").has_value()); // prompt changed
  REQUIRE_FALSE(cache::lookup("EDFT", 2, "xyz").has_value()); // inputs changed
  REQUIRE_FALSE(cache::lookup("XXXX", 2, "abc").has_value()); // absent

  cache::clear();
}

TEST_CASE("score cache: file-version mismatch discards the file",
          "[score_cache]") {
  const std::string path = tmp_path("xpvt_scores_version.json");
  {
    std::ofstream out(path, std::ios::trunc);
    out << R"({"version":999,"scores":{"EDDS":{"score":7,"prompt_version":1,)"
           R"("input_sig":"sig1"}}})";
  }
  cache::clear();
  cache::load(path);
  REQUIRE_FALSE(cache::lookup("EDDS", 1, "sig1").has_value());

  fs::remove(path);
  cache::clear();
}
