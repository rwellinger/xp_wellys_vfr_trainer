/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef PERSISTENCE_KEYCHAIN_HPP
#define PERSISTENCE_KEYCHAIN_HPP

#include <string>

namespace persistence::keychain {

// macOS Keychain wrapper for cloud API keys. The single-argument overloads
// use the production OpenAI service/account
// ("com.xp_wellys_vfr_trainer.openai" / "default"); the explicit overloads
// take a service+account pair so the Mistral key can persist in parallel
// (service "com.xp_wellys_vfr_trainer.mistral").
//
// load() returns an empty string when no entry exists or when the Keychain
// call fails — callers must treat empty as "no key". On non-macOS builds all
// calls are stubbed and report no key.

bool save(const std::string &api_key);
std::string load();
bool remove();
bool has();

bool save(const std::string &service, const std::string &account,
          const std::string &api_key);
std::string load(const std::string &service, const std::string &account);
bool remove(const std::string &service, const std::string &account);
bool has(const std::string &service, const std::string &account);

} // namespace persistence::keychain

#endif // PERSISTENCE_KEYCHAIN_HPP
