/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 *
 * Provider-agnostic helpers shared by the OpenAI and Mistral cloud clients.
 * SDK-free + libcurl-free so it lives in the engine OBJECT library and the
 * Catch2 tests can link it directly.
 */

#ifndef BACKENDS_OPENAI_COMMON_HPP
#define BACKENDS_OPENAI_COMMON_HPP

#include <string>

namespace backends::openai_common {

// Default endpoint root; constructor-injectable so unit tests can point
// at a local mock HTTP server.
inline constexpr const char *kDefaultBaseUrl = "https://api.openai.com";

// Return the last 4 characters of `api_key` (or the whole string if
// shorter). Used in audit-log lines so we identify which key was used
// without ever leaking the full secret.
std::string last4(const std::string &api_key);

} // namespace backends::openai_common

#endif // BACKENDS_OPENAI_COMMON_HPP
