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

#include "backends/loader.hpp"
#include "backends/manager.hpp"
#include "core/logging.hpp"
#include "persistence/settings.hpp"
#include "ui/trainer_ui.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>

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

static float flight_loop_cb(float, float, int, void *) {
  // X-Plane is C; any std::exception that propagates out is a guaranteed
  // crash. Service the async LM callback queue inside an exception boundary.
  try {
    backends::drain_callback_queue();
  } catch (const std::exception &e) {
    logging::error("flight_loop_cb threw: %s", e.what());
  } catch (...) {
    logging::error("flight_loop_cb threw an unknown exception");
  }
  return -1.0f; // called every frame
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
