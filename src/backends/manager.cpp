/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "backends/manager.hpp"

#include "core/logging.hpp"

#include <curl/curl.h>
#if defined(__APPLE__)
#include <pthread/qos.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace backends {

namespace {

std::mutex g_backend_mtx;
std::unique_ptr<ILanguageModel> g_lm;

// Serialises calls into the model and the teardown path. We do not
// parallelise multiple inferences; the trainer's two use cases are one-shot.
std::mutex g_lm_call_mtx;

// Pending main-thread callbacks. Worker threads enqueue here; the X-Plane
// flight loop drains via drain_callback_queue().
std::mutex g_cb_mtx;
std::deque<std::function<void()>> g_callbacks;

// Live worker thread count. stop() waits until this hits zero so a detached
// worker does not race the plugin unload.
std::atomic<int> g_active_workers{0};

void enqueue_callback(std::function<void()> fn) {
  std::lock_guard<std::mutex> lk(g_cb_mtx);
  g_callbacks.emplace_back(std::move(fn));
}

// Spawn a detached worker. The atomic counter tracks lifetime so stop() can
// wait for them; we don't keep std::thread handles around. Workers run at
// QOS_CLASS_UTILITY so the macOS scheduler deprioritizes them against
// X-Plane's renderer thread.
template <class Fn> void spawn_worker(Fn &&fn) {
  g_active_workers.fetch_add(1, std::memory_order_relaxed);
  std::thread t([fn = std::forward<Fn>(fn)]() mutable {
#if defined(__APPLE__)
    // QoS hint is a Darwin concern; the cloud-only Windows build has no
    // renderer-thread contention to deprioritize against, so it is a no-op
    // there rather than a deferred port.
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif
    fn();
    g_active_workers.fetch_sub(1, std::memory_order_relaxed);
  });
  t.detach();
}

// Shared body for respond_async / respond_json_async — the only difference is
// which ILanguageModel entry point the worker calls.
void dispatch(std::string system_prompt, std::string user_text, bool json_mode,
              std::function<void(std::string, bool)> callback) {
  if (!callback)
    return;
  if (!lm_ready()) {
    enqueue_callback([cb = std::move(callback)]() { cb({}, false); });
    return;
  }

  spawn_worker([system_prompt = std::move(system_prompt),
                user_text = std::move(user_text), json_mode,
                cb = std::move(callback)]() mutable {
    std::string reply;
    {
      std::lock_guard<std::mutex> lk(g_lm_call_mtx);
      ILanguageModel *lm_ptr = nullptr;
      {
        std::lock_guard<std::mutex> lk2(g_backend_mtx);
        lm_ptr = g_lm.get();
      }
      if (lm_ptr) {
        reply = json_mode ? lm_ptr->respond_json(system_prompt, user_text)
                          : lm_ptr->respond(system_prompt, user_text);
      }
    }
    bool ok = !reply.empty();
    enqueue_callback([cb = std::move(cb), reply = std::move(reply),
                      ok]() mutable { cb(std::move(reply), ok); });
  });
}

} // namespace

// ── Lifecycle ────────────────────────────────────────────────────────

void init() {
  // libcurl global init must run before any thread calls curl_easy_init().
  // On macOS the lazy auto-init is not thread-safe and can crash on the
  // first call from a worker thread. Idempotent: second calls return OK.
  CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (rc != CURLE_OK) {
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "[xp_wellys_vfr_trainer][ERROR] curl_global_init failed: %s\n",
                  curl_easy_strerror(rc));
    std::fputs(buf, stderr);
  }
}

void stop() {
  // Wait for any in-flight worker to finish before tearing down. Cap the
  // wait so a wedged worker cannot block plugin unload indefinitely.
  using clock = std::chrono::steady_clock;
  auto deadline = clock::now() + std::chrono::seconds(5);
  while (g_active_workers.load() > 0 && clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  {
    std::lock_guard<std::mutex> lk(g_backend_mtx);
    g_lm.reset();
  }
  {
    std::lock_guard<std::mutex> lk(g_cb_mtx);
    g_callbacks.clear();
  }

  curl_global_cleanup();
}

void register_lm(std::unique_ptr<ILanguageModel> lm) {
  std::lock_guard<std::mutex> lk(g_backend_mtx);
  g_lm = std::move(lm);
}

bool lm_ready() {
  std::lock_guard<std::mutex> lk(g_backend_mtx);
  return g_lm != nullptr;
}

void drain_callback_queue() {
  // Swap into a local deque, run outside the lock so callbacks that
  // re-enqueue (uncommon but legal) do not deadlock.
  std::deque<std::function<void()>> local;
  {
    std::lock_guard<std::mutex> lk(g_cb_mtx);
    local.swap(g_callbacks);
  }
  for (auto &cb : local) {
    if (cb)
      cb();
  }
}

// ── LM ───────────────────────────────────────────────────────────────

namespace lm {

void respond_async(std::string system_prompt, std::string user_text,
                   std::function<void(std::string, bool)> callback) {
  dispatch(std::move(system_prompt), std::move(user_text), /*json_mode=*/false,
           std::move(callback));
}

void respond_json_async(std::string system_prompt, std::string user_text,
                        std::function<void(std::string, bool)> callback) {
  dispatch(std::move(system_prompt), std::move(user_text), /*json_mode=*/true,
           std::move(callback));
}

} // namespace lm

} // namespace backends
