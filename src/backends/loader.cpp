/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "backends/loader.hpp"

#include "backends/manager.hpp"
#include "backends/mistral_lm.hpp"
#include "backends/openai_lm.hpp"
#include "core/logging.hpp"
#include "persistence/settings.hpp"

#include <memory>
#include <string>

namespace backends::loader {

void start() {
  const std::string mode = settings::backend_mode();

  if (mode == "openai") {
    std::string api_key = settings::load_api_key();
    if (api_key.empty()) {
      logging::error("OpenAI mode active but no API key in Keychain — open the "
                     "plugin window and paste a key");
      register_lm(nullptr);
      return;
    }
    logging::info("BACKEND MODE: OPENAI (api.openai.com), model %s",
                  settings::openai_lm_model().c_str());
    register_lm(std::make_unique<OpenAiLm>(api_key, settings::openai_lm_model()));
  } else if (mode == "mistral") {
    std::string api_key = settings::load_mistral_api_key();
    if (api_key.empty()) {
      logging::error("Mistral mode active but no API key in Keychain — open the "
                     "plugin window and paste a key");
      register_lm(nullptr);
      return;
    }
    logging::info("BACKEND MODE: MISTRAL (api.mistral.ai), model %s",
                  settings::mistral_lm_model().c_str());
    register_lm(
        std::make_unique<MistralLm>(api_key, settings::mistral_lm_model()));
  } else {
    logging::error("Unknown backend_mode '%s' — expected openai or mistral",
                   mode.c_str());
    register_lm(nullptr);
  }
}

void stop() { register_lm(nullptr); }

} // namespace backends::loader
