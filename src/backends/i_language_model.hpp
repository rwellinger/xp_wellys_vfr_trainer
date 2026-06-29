/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef BACKENDS_I_LANGUAGE_MODEL_HPP
#define BACKENDS_I_LANGUAGE_MODEL_HPP

#include <string>

namespace backends {

class ILanguageModel {
public:
  virtual ~ILanguageModel() = default;

  // Run a single completion with the given system prompt + user message.
  // Implementations must clear any per-call state so a long-running
  // orchestrator can hand them an unbounded sequence of independent turns.
  // Returns the assistant reply (empty on failure).
  virtual std::string respond(const std::string &system_prompt,
                              const std::string &user_text) = 0;

  // Same contract as respond(), but the output is requested as a JSON
  // object (the cloud providers enable response_format=json_object; a
  // local backend would constrain via grammar). The trainer's two LLM
  // use cases — airport-difficulty score and post-flight evaluation —
  // both expect JSON, so this is the entry point they will use. Default
  // impl falls back to respond() so test mocks keep working unchanged.
  virtual std::string respond_json(const std::string &system_prompt,
                                   const std::string &user_text) {
    return respond(system_prompt, user_text);
  }
};

} // namespace backends

#endif // BACKENDS_I_LANGUAGE_MODEL_HPP
