/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "backends/openai_common.hpp"

namespace backends::openai_common {

std::string last4(const std::string &api_key) {
  if (api_key.size() <= 4)
    return api_key;
  return api_key.substr(api_key.size() - 4);
}

} // namespace backends::openai_common
