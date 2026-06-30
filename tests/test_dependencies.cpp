/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include <catch2/catch_amalgamated.hpp>

#include "dependencies/dependencies.hpp"

#include <map>
#include <string>

using namespace deps;

namespace {

// Fake probe so evaluate() runs without the X-Plane SDK (mirrors MockLm in
// test_backends.cpp). Signatures not in the map are treated as not installed.
class FakeProbe final : public IPluginProbe {
public:
  std::string root = "/XP/";
  std::map<std::string, PluginStatus> table;

  PluginStatus probe(const std::string &sig) override {
    auto it = table.find(sig);
    return it == table.end() ? PluginStatus{} : it->second;
  }
  std::string xplane_root() override { return root; }
};

const std::string kAtcSig = "ch.thWelly.wellys_devfr_atc";
const std::string kPilotSig = "thWelly.xp_pilot";

const DependencyState *by_signature(const std::vector<DependencyState> &v,
                                    const std::string &sig) {
  for (const auto &s : v)
    if (s.signature == sig)
      return &s;
  return nullptr;
}

} // namespace

TEST_CASE("evaluate resolves output paths from the X-Plane root", "[deps]") {
  FakeProbe probe;
  probe.root = "/XP/";
  auto v = evaluate(probe);
  REQUIRE(v.size() == 2);

  const DependencyState *atc = by_signature(v, kAtcSig);
  const DependencyState *pilot = by_signature(v, kPilotSig);
  REQUIRE(atc != nullptr);
  REQUIRE(pilot != nullptr);
  REQUIRE(atc->output_path == "/XP/Output/xp_wellys_devfr_atc/flightlog");
  REQUIRE(pilot->output_path == "/XP/Output/x_pilot_reports/flights");
}

TEST_CASE("all_ready true only when both are installed and enabled", "[deps]") {
  FakeProbe probe;
  probe.table[kAtcSig] = {true, true};
  probe.table[kPilotSig] = {true, true};
  REQUIRE(all_ready(evaluate(probe)));
}

TEST_CASE("missing plugin blocks readiness", "[deps]") {
  FakeProbe probe;
  // ATC absent (not in table), xp_pilot present.
  probe.table[kPilotSig] = {true, true};
  auto v = evaluate(probe);
  REQUIRE_FALSE(all_ready(v));

  const DependencyState *atc = by_signature(v, kAtcSig);
  REQUIRE(atc != nullptr);
  REQUIRE_FALSE(atc->installed);
  REQUIRE_FALSE(atc->enabled);
}

TEST_CASE("installed-but-disabled plugin blocks readiness", "[deps]") {
  FakeProbe probe;
  probe.table[kAtcSig] = {true, true};
  probe.table[kPilotSig] = {true, false}; // installed, not enabled
  auto v = evaluate(probe);
  REQUIRE_FALSE(all_ready(v));

  const DependencyState *pilot = by_signature(v, kPilotSig);
  REQUIRE(pilot != nullptr);
  REQUIRE(pilot->installed);
  REQUIRE_FALSE(pilot->enabled);
}
