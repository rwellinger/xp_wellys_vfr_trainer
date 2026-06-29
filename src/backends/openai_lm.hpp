/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef BACKENDS_OPENAI_LM_HPP
#define BACKENDS_OPENAI_LM_HPP

#include "backends/i_language_model.hpp"
#include "backends/openai_common.hpp"

#include <string>

namespace backends {

// ILanguageModel backed by OpenAI's /v1/chat/completions endpoint.
// respond_json() enables the provider's JSON mode (response_format=
// json_object). Every call emits a [LM-OPENAI] audit log line.
class OpenAiLm final : public ILanguageModel {
public:
  OpenAiLm(std::string api_key, std::string model,
           std::string base_url = openai_common::kDefaultBaseUrl);

  std::string respond(const std::string &system_prompt,
                      const std::string &user_text) override;

  std::string respond_json(const std::string &system_prompt,
                           const std::string &user_text) override;

private:
  std::string call(const std::string &system_prompt,
                   const std::string &user_text, bool json_mode);

  std::string api_key_;
  std::string model_;
  std::string base_url_;
};

} // namespace backends

#endif // BACKENDS_OPENAI_LM_HPP
