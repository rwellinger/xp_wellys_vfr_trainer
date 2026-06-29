/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "persistence/settings.hpp"

#include "core/logging.hpp"
#include "persistence/keychain.hpp"

#include <XPLMPlugin.h>
#include <XPLMUtilities.h>

#include <json.hpp>

#include <fstream>
#include <sys/stat.h>

using json = nlohmann::json;

namespace settings {

namespace {

// Mistral keys live in a parallel Keychain entry so both providers can
// persist at once and a backend-mode flip never requires re-pasting a key.
constexpr const char *kMistralService = "com.xp_wellys_vfr_trainer.mistral";
constexpr const char *kKeyAccount = "default";

std::string data_dir_path = "data";
json cfg;

json default_config() {
  return json{
      {"backend_mode", "openai"},
      {"openai_lm_model", "gpt-4o-mini"},
      {"mistral_lm_model", "mistral-large-latest"},
      {"api_key_saved", false},
      {"mistral_api_key_saved", false},
      {"debug_logging", false},
      {"window_x", -1.0},
      {"window_y", -1.0},
      {"window_w", -1.0},
      {"window_h", -1.0},
  };
}

} // namespace

void init() {
  // Resolve plugin path to find the data/ directory. Installed layout:
  //   .../plugins/xp_wellys_vfr_trainer/mac_x64/xp_wellys_vfr_trainer.xpl
  // Go up two levels (strip filename, strip mac_x64/) to reach the plugin
  // root, then append /data.
  char plugin_path_raw[2048] = {};
  XPLMGetPluginInfo(XPLMGetMyID(), nullptr, plugin_path_raw, nullptr, nullptr);

  std::string path_str(plugin_path_raw);
#if defined(__APPLE__)
  // macOS may return an HFS path (colon-separated) — convert to POSIX.
  if (path_str.find(':') != std::string::npos &&
      path_str.find('/') == std::string::npos) {
    auto colon = path_str.find(':');
    std::string posix = path_str.substr(colon + 1);
    for (char &c : posix)
      if (c == ':')
        c = '/';
    path_str = "/" + posix;
  }
#endif

  auto pos = path_str.rfind('/');
  if (pos != std::string::npos)
    pos = path_str.rfind('/', pos - 1);
  if (pos != std::string::npos)
    data_dir_path = path_str.substr(0, pos) + "/data";
  else
    data_dir_path = "data";

  mkdir(data_dir_path.c_str(), 0755);

  std::string json_path = data_dir_path + "/settings.json";
  std::ifstream in(json_path);
  bool needs_save = false;
  if (in.good()) {
    try {
      in >> cfg;
      // Merge any missing defaults so a settings.json from an older build
      // gains new keys without losing user values.
      json defaults = default_config();
      for (auto &[key, value] : defaults.items()) {
        if (!cfg.contains(key))
          cfg[key] = value;
      }
    } catch (...) {
      logging::error("Failed to parse settings.json, using defaults");
      cfg = default_config();
      needs_save = true;
    }
  } else {
    cfg = default_config();
    needs_save = true;
  }

  if (needs_save)
    save();

  logging::info("Settings loaded (%s)", json_path.c_str());
}

void stop() {}

void save() {
  std::string json_path = data_dir_path + "/settings.json";
  std::ofstream out(json_path);
  if (out.good())
    out << cfg.dump(2) << std::endl;
}

std::string get_data_dir() { return data_dir_path; }

bool debug_logging() { return cfg.value("debug_logging", false); }
void set_debug_logging(bool v) {
  cfg["debug_logging"] = v;
  save();
}

std::string backend_mode() {
  return cfg.value("backend_mode", std::string("openai"));
}
void set_backend_mode(const std::string &v) {
  cfg["backend_mode"] = v;
  save();
}

std::string openai_lm_model() {
  return cfg.value("openai_lm_model", std::string("gpt-4o-mini"));
}
void set_openai_lm_model(const std::string &v) {
  cfg["openai_lm_model"] = v;
  save();
}

std::string mistral_lm_model() {
  return cfg.value("mistral_lm_model", std::string("mistral-large-latest"));
}
void set_mistral_lm_model(const std::string &v) {
  cfg["mistral_lm_model"] = v;
  save();
}

bool api_key_saved() { return cfg.value("api_key_saved", false); }
bool mistral_api_key_saved() {
  return cfg.value("mistral_api_key_saved", false);
}

bool save_api_key(const std::string &key) {
  if (!persistence::keychain::save(key))
    return false;
  cfg["api_key_saved"] = true;
  save();
  return true;
}
std::string load_api_key() { return persistence::keychain::load(); }
void delete_api_key() {
  persistence::keychain::remove();
  cfg["api_key_saved"] = false;
  save();
}

bool save_mistral_api_key(const std::string &key) {
  if (!persistence::keychain::save(kMistralService, kKeyAccount, key))
    return false;
  cfg["mistral_api_key_saved"] = true;
  save();
  return true;
}
std::string load_mistral_api_key() {
  return persistence::keychain::load(kMistralService, kKeyAccount);
}
void delete_mistral_api_key() {
  persistence::keychain::remove(kMistralService, kKeyAccount);
  cfg["mistral_api_key_saved"] = false;
  save();
}

float window_x() { return cfg.value("window_x", -1.0f); }
float window_y() { return cfg.value("window_y", -1.0f); }
float window_w() { return cfg.value("window_w", -1.0f); }
float window_h() { return cfg.value("window_h", -1.0f); }
void set_window_geometry(float x, float y, float w, float h) {
  cfg["window_x"] = x;
  cfg["window_y"] = y;
  cfg["window_w"] = w;
  cfg["window_h"] = h;
}
void reset_window_geometry() {
  cfg["window_x"] = -1.0f;
  cfg["window_y"] = -1.0f;
  cfg["window_w"] = -1.0f;
  cfg["window_h"] = -1.0f;
  save();
}

} // namespace settings
