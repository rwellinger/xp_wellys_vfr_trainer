/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 *
 * Exercises the async LM dispatcher (manager.cpp) end-to-end against a mock
 * ILanguageModel — no network, no X-Plane SDK.
 */

#include "backends/i_language_model.hpp"
#include "backends/manager.hpp"

#include <catch2/catch_amalgamated.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace {

// Mock that echoes the user text back, tagged so we can tell respond() from
// respond_json().
class MockLm final : public backends::ILanguageModel {
public:
  std::string respond(const std::string &, const std::string &user) override {
    return "echo:" + user;
  }
  std::string respond_json(const std::string &,
                           const std::string &user) override {
    return "json:" + user;
  }
};

// Spin the main-thread callback drain until `done` flips or a deadline passes.
// Mirrors what the X-Plane flight loop does each frame.
void pump_until(std::atomic<bool> &done) {
  using clock = std::chrono::steady_clock;
  auto deadline = clock::now() + std::chrono::seconds(5);
  while (!done.load() && clock::now() < deadline) {
    backends::drain_callback_queue();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  backends::drain_callback_queue();
}

} // namespace

TEST_CASE("respond_async dispatches the reply via the callback", "[backends]") {
  backends::register_lm(std::make_unique<MockLm>());
  REQUIRE(backends::lm_ready());

  std::atomic<bool> done{false};
  std::string result;
  bool ok = false;
  backends::lm::respond_async("sys", "hello",
                              [&](std::string text, bool success) {
                                result = std::move(text);
                                ok = success;
                                done.store(true);
                              });

  pump_until(done);

  REQUIRE(done.load());
  REQUIRE(ok);
  REQUIRE(result == "echo:hello");

  backends::register_lm(nullptr);
}

TEST_CASE("respond_json_async routes to respond_json", "[backends]") {
  backends::register_lm(std::make_unique<MockLm>());

  std::atomic<bool> done{false};
  std::string result;
  backends::lm::respond_json_async("sys", "world",
                                   [&](std::string text, bool success) {
                                     (void)success;
                                     result = std::move(text);
                                     done.store(true);
                                   });

  pump_until(done);

  REQUIRE(done.load());
  REQUIRE(result == "json:world");

  backends::register_lm(nullptr);
}

TEST_CASE("respond_async reports failure when no LM is registered",
          "[backends]") {
  backends::register_lm(nullptr);
  REQUIRE_FALSE(backends::lm_ready());

  std::atomic<bool> done{false};
  bool ok = true;
  backends::lm::respond_async("sys", "hello",
                              [&](std::string, bool success) {
                                ok = success;
                                done.store(true);
                              });

  // The no-LM path enqueues the failure callback synchronously; one drain
  // dispatches it.
  backends::drain_callback_queue();

  REQUIRE(done.load());
  REQUIRE_FALSE(ok);
}
