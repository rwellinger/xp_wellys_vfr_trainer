/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include <string>

namespace settings {

void init();
void stop();
void save();

// Plugin-relative data directory (<plugin>/data). Holds settings.json and
// imgui.ini.
std::string get_data_dir();

bool debug_logging();
void set_debug_logging(bool v);

// Backend selection: "openai" or "mistral". Picks which cloud LM the loader
// registers. Default "openai".
std::string backend_mode();
void set_backend_mode(const std::string &v);

std::string openai_lm_model();
void set_openai_lm_model(const std::string &v);
std::string mistral_lm_model();
void set_mistral_lm_model(const std::string &v);

// Post-flight report language: "de" or "en". Default "de". Controls the LLM
// report text and the Post-Flight tab's UI labels.
std::string report_language();
void set_report_language(const std::string &v);

// True when an OpenAI / Mistral API key was saved to the Keychain. The keys
// themselves are never persisted to settings.json — only these flags.
bool api_key_saved();
bool mistral_api_key_saved();

// Keychain-backed key handling. save_*() also updates the *_saved flag and
// persists settings.json. load_*() returns empty when no key is stored.
bool save_api_key(const std::string &key);
std::string load_api_key();
void delete_api_key();

bool save_mistral_api_key(const std::string &key);
std::string load_mistral_api_key();
void delete_mistral_api_key();

// Window geometry (-1 = use default/center).
float window_x();
float window_y();
float window_w();
float window_h();
void set_window_geometry(float x, float y, float w, float h);
void reset_window_geometry();

} // namespace settings

#endif // SETTINGS_HPP
