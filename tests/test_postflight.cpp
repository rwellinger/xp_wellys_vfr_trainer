/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 *
 * Post-flight evaluation (#6): JSON parsing, ATC<->flight time correlation,
 * the report cache, and the LLM evaluator (against a mock backend). SDK-free.
 */

#include "backends/i_language_model.hpp"
#include "backends/manager.hpp"
#include "postflight/atc_log.hpp"
#include "postflight/correlation.hpp"
#include "postflight/discovery.hpp"
#include "postflight/evaluator.hpp"
#include "postflight/flight_log.hpp"
#include "postflight/report_cache.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

using namespace postflight;

namespace {

const std::string kAtcFixture =
    std::string(TESTDATA_DIR) + "/2026-06-30_1135_EDTZ-EDNY.json";
const std::string kFlightFixture =
    std::string(TESTDATA_DIR) + "/2026-06-30_EDTZ_EDNY_BN2P.json";

// Mock LM that returns a fixed post-flight JSON regardless of the prompt. The
// execution score is deliberately out of range (15) to exercise clamping.
class MockLm final : public backends::ILanguageModel {
public:
  std::string respond(const std::string &, const std::string &) override {
    return "unused";
  }
  std::string respond_json(const std::string &, const std::string &) override {
    // execution_score 15 -> clamped to 10; findings include one valid entry,
    // one with an out-of-range enum (normalised), and one empty text (dropped).
    return R"({"phraseology_score":8,"phraseology_rationale":"clear calls",)"
           R"("execution_score":15,"execution_rationale":"stable",)"
           R"("summary":"good circuit",)"
           R"("findings":[)"
           R"({"phase":"Initial call","category":"phraseology",)"
           R"("sentiment":"critique","text":"Callsign missing at the end."},)"
           R"({"phase":"Landing","category":"execution",)"
           R"("sentiment":"bogus","text":"Smooth touchdown."},)"
           R"({"phase":"Taxi","category":"phraseology",)"
           R"("sentiment":"praise","text":""}]})";
  }
};

// Drain the main-thread callback queue until the evaluator leaves Running (or a
// deadline passes) — the same drain the flight loop runs each frame.
void pump_until_settled() {
  using clock = std::chrono::steady_clock;
  auto deadline = clock::now() + std::chrono::seconds(5);
  while (evaluator::status() == evaluator::Status::Running &&
         clock::now() < deadline) {
    backends::drain_callback_queue();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  backends::drain_callback_queue();
}

} // namespace

TEST_CASE("parse_atc_log reads the v2 fixture", "[postflight]") {
  const AtcLog log = load_atc_log(kAtcFixture);
  REQUIRE(log.version == 2);
  REQUIRE(log.flight.departure_airport == "EDTZ");
  REQUIRE(log.flight.destination_airport == "EDNY");
  REQUIRE(log.flight.started_at_epoch == 1782812143);
  REQUIRE(log.transmissions.size() == 9);
  REQUIRE(log.transmissions.front().ts == 1782812143);
  REQUIRE(log.transmissions.front().intent == "INITIAL_CALL_GROUND");
  REQUIRE(log.transmissions.back().ts == 1782813314);
}

TEST_CASE("parse_atc_log rejects v1 and missing ts", "[postflight]") {
  // Version 1: no epoch fields at all.
  const std::string v1 = R"({"version":1,"flight":{"started_at":"x",)"
                         R"("departure_airport":"EDTZ"},"transmissions":[]})";
  REQUIRE_THROWS_AS(parse_atc_log(v1), std::runtime_error);

  // Version 2 header but a transmission without ts.
  const std::string no_ts =
      R"({"version":2,"flight":{"started_at_epoch":1,"departure_airport":"EDTZ"},)"
      R"("transmissions":[{"time":"x","transcript":"y"}]})";
  REQUIRE_THROWS_AS(parse_atc_log(no_ts), std::runtime_error);

  // Version 2 but flight.started_at_epoch missing.
  const std::string no_epoch =
      R"({"version":2,"flight":{"departure_airport":"EDTZ"},"transmissions":[]})";
  REQUIRE_THROWS_AS(parse_atc_log(no_epoch), std::runtime_error);
}

TEST_CASE("parse_flight_log reads the xp_pilot fixture", "[postflight]") {
  const FlightLog fl = load_flight_log(kFlightFixture);
  REQUIRE(fl.departure_icao == "EDTZ");
  REQUIRE(fl.arrival_icao == "EDNY");
  REQUIRE(fl.start_time == 1782812450);
  REQUIRE(fl.aircraft_icao == "BN2P");
  REQUIRE(fl.track.size() == 77);
  REQUIRE(fl.track.front().t == 1782812476);
  REQUIRE(fl.track.front().alt_ft == 1561);
  REQUIRE(fl.landings.size() == 1);
  REQUIRE(fl.landings.front().rating == "BUTTER!");
}

TEST_CASE("correlate joins transmissions to nearest track point",
          "[postflight]") {
  const AtcLog atc = load_atc_log(kAtcFixture);
  const FlightLog fl = load_flight_log(kFlightFixture);
  const CorrelatedTimeline tl = correlate(atc, fl);

  REQUIRE(tl.events.size() == atc.transmissions.size());

  // First transmission (ts 1782812143, parked initial call) is 333 s before the
  // first track sample (1782812476) — outside tolerance, so no position.
  REQUIRE(tl.events.front().tx.ts == 1782812143);
  REQUIRE_FALSE(tl.events.front().has_position);

  // Second transmission (ts 1782812544) sits 2 s from track point 1782812546
  // (alt 2523) — matched.
  const auto &second = tl.events.at(1);
  REQUIRE(second.tx.ts == 1782812544);
  REQUIRE(second.has_position);
  REQUIRE(second.dt <= kCorrelationToleranceS);
  REQUIRE(second.track.alt_ft == 2523);
}

TEST_CASE("correlate tolerance excludes distant track points", "[postflight]") {
  const AtcLog atc = load_atc_log(kAtcFixture);
  const FlightLog fl = load_flight_log(kFlightFixture);
  // A 1 s tolerance leaves only exact-ish matches; the parked first call still
  // has no position.
  const CorrelatedTimeline tl = correlate(atc, fl, 1);
  REQUIRE_FALSE(tl.events.front().has_position);
}

TEST_CASE("same_flight matches the fixture pair", "[postflight]") {
  const AtcLog atc = load_atc_log(kAtcFixture);
  const FlightLog fl = load_flight_log(kFlightFixture);
  REQUIRE(same_flight(atc, fl));

  FlightLog other = fl;
  other.departure_icao = "LSZH";
  REQUIRE_FALSE(same_flight(atc, other));
}

TEST_CASE("report_filename is UTC date_time + route", "[postflight]") {
  report_cache::Entry e;
  e.departure_icao = "EDTZ";
  e.arrival_icao = "EDNY";
  e.started_at_epoch = 1782812143; // 2026-06-30 09:35:43 UTC (11:35 local)
  REQUIRE(report_cache::report_filename(e) ==
          "2026-06-30_0935_EDTZ-EDNY_trainingsreport.json");

  // No arrival -> no "-<DEST>" suffix.
  e.arrival_icao.clear();
  REQUIRE(report_cache::report_filename(e) ==
          "2026-06-30_0935_EDTZ_trainingsreport.json");
}

TEST_CASE("report_cache round-trips through a directory", "[postflight]") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "wvt_reports_test";
  std::error_code ec;
  fs::remove_all(dir, ec);

  report_cache::clear();
  report_cache::load(dir.string()); // creates the dir; empty to start

  report_cache::Entry e;
  e.departure_icao = "EDTZ";
  e.arrival_icao = "EDNY";
  e.started_at_epoch = 1782812143;
  e.phraseology_score = 8;
  e.execution_score = 7;
  e.summary = "ok";
  e.findings.push_back({"Downwind", "phraseology", "critique", "Late report."});
  e.language = "de";
  e.prompt_version = evaluator::kPromptVersion;
  const std::string key = report_cache::make_key("EDTZ", 1782812143);
  report_cache::put(key, e);
  report_cache::save();

  // One per-flight file with the expected name landed in the directory.
  REQUIRE(fs::exists(dir / "2026-06-30_0935_EDTZ-EDNY_trainingsreport.json"));

  // Reload into a fresh state and confirm the entry (incl. findings) survived.
  report_cache::clear();
  report_cache::load(dir.string());
  const report_cache::Entry *got = report_cache::lookup(key);
  REQUIRE(got != nullptr);
  REQUIRE(got->departure_icao == "EDTZ");
  REQUIRE(got->phraseology_score == 8);
  REQUIRE(got->language == "de");
  REQUIRE(got->findings.size() == 1);
  REQUIRE(got->findings.front().phase == "Downwind");
  REQUIRE(report_cache::latest() != nullptr);
  REQUIRE(report_cache::latest()->started_at_epoch == 1782812143);

  report_cache::clear();
  fs::remove_all(dir, ec);
}

TEST_CASE("system_prompt appends a language directive", "[postflight]") {
  const std::string de = evaluator::system_prompt("de");
  const std::string en = evaluator::system_prompt("en");
  // Base rubric is preserved and prefixed.
  REQUIRE(de.rfind(evaluator::kSystemPrompt, 0) == 0);
  REQUIRE(en.rfind(evaluator::kSystemPrompt, 0) == 0);
  REQUIRE(de.find("in German") != std::string::npos);
  REQUIRE(en.find("in English") != std::string::npos);
}

TEST_CASE("evaluator scores a flight via the mock backend", "[postflight]") {
  report_cache::clear();
  report_cache::load(""); // in-memory only (empty path -> save() is a no-op)
  evaluator::stop();
  evaluator::set_model("mock:test");
  backends::register_lm(std::make_unique<MockLm>());

  const AtcLog atc = load_atc_log(kAtcFixture);
  const FlightLog fl = load_flight_log(kFlightFixture);

  // build_prompt surfaces the route and the landing summary.
  const std::string prompt = evaluator::build_prompt(correlate(atc, fl), fl);
  REQUIRE(prompt.find("EDTZ -> EDNY") != std::string::npos);
  REQUIRE(prompt.find("BUTTER!") != std::string::npos);

  evaluator::evaluate_async(atc, fl, "de");
  REQUIRE(evaluator::status() == evaluator::Status::Running);

  pump_until_settled();

  REQUIRE(evaluator::status() == evaluator::Status::Done);
  const report_cache::Entry *e =
      report_cache::lookup(evaluator::last_key());
  REQUIRE(e != nullptr);
  REQUIRE(e->phraseology_score == 8);
  REQUIRE(e->execution_score == 10); // 15 clamped to 10
  REQUIRE(e->summary == "good circuit");
  REQUIRE(e->model == "mock:test");
  REQUIRE(e->language == "de");
  // Two of three findings survive: empty-text dropped, bogus sentiment ->
  // "critique".
  REQUIRE(e->findings.size() == 2);
  REQUIRE(e->findings.front().phase == "Initial call");
  REQUIRE(e->findings.front().sentiment == "critique");
  REQUIRE(e->findings.at(1).sentiment == "critique"); // normalised from "bogus"

  backends::register_lm(nullptr);
  evaluator::stop();
  report_cache::clear();
}
