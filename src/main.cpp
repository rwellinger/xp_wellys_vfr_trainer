/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version. See <https://www.gnu.org/licenses/>.
 */

#include <XPLMDisplay.h>
#include <XPLMMenus.h>
#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include "airports/airport_db.hpp"
#include "airports/airport_score_cache.hpp"
#include "airports/airport_scorer.hpp"
#include "backends/loader.hpp"
#include "backends/manager.hpp"
#include "core/logging.hpp"
#include "persistence/settings.hpp"
#include "postflight/evaluator.hpp"
#include "postflight/report_cache.hpp"
#include "ui/trainer_ui.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

static XPLMMenuID menu_id = nullptr;
static int menu_container_idx = -1;
static XPLMCommandRef cmd_toggle_ = nullptr;

static void menu_handler(void *, void *item_ref) {
  intptr_t idx = reinterpret_cast<intptr_t>(item_ref);
  if (idx == 0)
    trainer_ui::toggle();
  else if (idx == 1)
    trainer_ui::reset_window_position();
}

static int toggle_cmd_handler(XPLMCommandRef, XPLMCommandPhase phase, void *) {
  if (phase == xplm_CommandBegin)
    trainer_ui::toggle();
  return 0;
}

// Path to the X-Plane Global Airports apt.dat. XPLM_USE_NATIVE_PATHS is enabled
// in XPluginStart, so XPLMGetSystemPath returns a POSIX path.
static std::string global_apt_dat_path() {
  char raw[2048] = {};
  XPLMGetSystemPath(raw);
  std::string p(raw);
  if (!p.empty() && p.back() != '/')
    p += '/';
  return p + "Global Scenery/Global Airports/Earth nav data/apt.dat";
}

// Directory the trainer writes its post-flight reports to (#21), mirroring the
// other plugins' <X-Plane>/Output/<plugin>/ convention.
static std::string trainer_output_dir() {
  char raw[2048] = {};
  XPLMGetSystemPath(raw);
  std::string p(raw);
  if (!p.empty() && p.back() != '/')
    p += '/';
  return p + "Output/xp_wellys_vfr_trainer";
}

static float flight_loop_cb(float, float, int, void *) {
  // X-Plane is C; any std::exception that propagates out is a guaranteed
  // crash. Service the async LM callback queue inside an exception boundary.
  try {
    backends::drain_callback_queue();
    // Drive lazy LLM airport scoring (one call at a time when a backend is
    // ready); runs even while the window is closed.
    airports::scorer::pump();
  } catch (const std::exception &e) {
    logging::error("flight_loop_cb threw: %s", e.what());
  } catch (...) {
    logging::error("flight_loop_cb threw an unknown exception");
  }
  return -1.0f; // called every frame
}

// "provider:model" tag for score provenance, from the current settings.
static std::string current_model_tag() {
  const std::string mode = settings::backend_mode();
  const std::string model =
      (mode == "mistral") ? settings::mistral_lm_model() : settings::openai_lm_model();
  return mode + ":" + model;
}

PLUGIN_API int XPluginStart(char *name, char *sig, char *desc) {
  // Required for X-Plane installs on external volumes.
  XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

#ifdef XP_WELLYS_TRAINER_VERSION
  std::snprintf(name, 256, "Welly's VFR Trainer v%s", XP_WELLYS_TRAINER_VERSION);
#else
  std::snprintf(name, 256, "Welly's VFR Trainer");
#endif
  std::snprintf(sig, 256, "ch.thWelly.wellys_vfr_trainer");
  std::snprintf(desc, 256, "VFR training gamification layer for X-Plane 12");

  logging::set_sink(&XPLMDebugString);
  logging::info("Plugin started");

  settings::init();
  backends::init();
  trainer_ui::init();

  // Load the cached LLM difficulty scores (one JSON file in the plugin's data
  // directory). The scorer fills it lazily as airports show up as suggestions.
  airports::score_cache::load(settings::get_data_dir() + "/airport_scores.json");
  airports::scorer::set_model(current_model_tag());

  // Load cached post-flight session reports (#6/#21). One file per flight under
  // <X-Plane>/Output/xp_wellys_vfr_trainer/; the evaluator writes here when the
  // user scores a flight. Same provenance tag as the airport scorer.
  postflight::report_cache::load(trainer_output_dir());
  postflight::evaluator::set_model(current_model_tag());

  // Load + DACH-filter the apt.dat on a background worker (parsing the ~1 GB
  // file takes ~1.4 s; the main thread must not block). Joined in XPluginStop.
  airports::airport_db::start_load(global_apt_dat_path());

  // Flight loop — drains async LM callbacks every frame.
  XPLMCreateFlightLoop_t loop_params{};
  loop_params.structSize = sizeof(loop_params);
  loop_params.phase = xplm_FlightLoop_Phase_AfterFlightModel;
  loop_params.callbackFunc = flight_loop_cb;
  XPLMFlightLoopID loop_id = XPLMCreateFlightLoop(&loop_params);
  XPLMScheduleFlightLoop(loop_id, -1.0f, true);

  // Toggle command (bindable via X-Plane keyboard/joystick settings).
  cmd_toggle_ = XPLMCreateCommand("xp_wellys_vfr_trainer/toggle_window",
                                  "Toggle VFR Trainer Window");
  XPLMRegisterCommandHandler(cmd_toggle_, toggle_cmd_handler, 1, nullptr);

  // Menu.
  menu_container_idx = XPLMAppendMenuItem(XPLMFindPluginsMenu(),
                                          "Welly's VFR Trainer", nullptr, 0);
  menu_id = XPLMCreateMenu("Welly's VFR Trainer", XPLMFindPluginsMenu(),
                           menu_container_idx, menu_handler, nullptr);
  XPLMAppendMenuItem(menu_id, "Open / Close", nullptr, 0);
  // NOLINTBEGIN(performance-no-int-to-ptr)
  XPLMAppendMenuItem(menu_id, "Reset Window Position",
                     reinterpret_cast<void *>(uintptr_t{1}), 0);
  // NOLINTEND(performance-no-int-to-ptr)

  return 1;
}

PLUGIN_API void XPluginStop() {
  if (menu_id) {
    XPLMDestroyMenu(menu_id);
    menu_id = nullptr;
  }

  trainer_ui::stop();
  // Stop scheduling new scoring and flush the score cache to disk.
  airports::scorer::stop();
  airports::score_cache::save();
  // Reset the post-flight evaluator (its reports are persisted on each score).
  postflight::evaluator::stop();
  // Join the apt.dat loader worker (no worker may outlive XPluginStop).
  airports::airport_db::stop();
  // backends::stop() waits for in-flight workers, drops the LM, and runs
  // curl_global_cleanup.
  backends::stop();
  settings::stop();

  logging::info("Plugin stopped");
}

PLUGIN_API int XPluginEnable() {
  try {
    // Register the cloud LM for the configured backend. Cheap + synchronous
    // (no model download), so it runs inline rather than on a worker.
    backends::loader::start();
    return 1;
  } catch (const std::exception &e) {
    logging::error("XPluginEnable threw: %s", e.what());
    return 0;
  } catch (...) {
    logging::error("XPluginEnable threw an unknown exception");
    return 0;
  }
}

PLUGIN_API void XPluginDisable() { backends::loader::stop(); }

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void *) {}
