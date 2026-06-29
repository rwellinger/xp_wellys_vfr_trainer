/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef BACKENDS_MANAGER_HPP
#define BACKENDS_MANAGER_HPP

#include "backends/i_language_model.hpp"

#include <functional>
#include <memory>
#include <string>

namespace backends {

// Lifecycle. init() runs curl_global_init on the main thread before any
// worker spawns; stop() waits for in-flight workers, drops the registered
// backend, and runs curl_global_cleanup. init() is idempotent.
void init();
void stop();

// The plugin registers a concrete language model (OpenAiLm / MistralLm) once
// an API key is available. Pass nullptr to unregister. The async dispatchers
// below short-circuit to `success=false` when no backend is registered.
void register_lm(std::unique_ptr<ILanguageModel> lm);
bool lm_ready();

// Drain pending async callbacks on the X-Plane main thread. Call from the
// flight loop every frame.
void drain_callback_queue();

namespace lm {

// Run a completion on a worker thread; the callback is dispatched on the
// main thread via drain_callback_queue(). `success` is false when no
// backend is registered or the call returned empty.
void respond_async(std::string system_prompt, std::string user_text,
                   std::function<void(std::string text, bool success)> callback);

// Same as respond_async() but requests a JSON-object reply (the two trainer
// use cases — airport-difficulty score, post-flight evaluation — both expect
// JSON). Parsing the JSON is the caller's responsibility.
void respond_json_async(
    std::string system_prompt, std::string user_text,
    std::function<void(std::string text, bool success)> callback);

} // namespace lm

} // namespace backends

#endif // BACKENDS_MANAGER_HPP
