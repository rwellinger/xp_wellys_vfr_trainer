/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "dependencies/xplm_plugin_probe.hpp"

#include <XPLMPlugin.h>
#include <XPLMUtilities.h>

namespace deps {

PluginStatus XplmPluginProbe::probe(const std::string &signature) {
  XPLMPluginID id = XPLMFindPluginBySignature(signature.c_str());
  if (id == XPLM_NO_PLUGIN_ID)
    return {false, false};
  return {true, XPLMIsPluginEnabled(id) != 0};
}

std::string XplmPluginProbe::xplane_root() {
  // XPLM_USE_NATIVE_PATHS is enabled in XPluginStart, so this is a POSIX path
  // (same call backs global_apt_dat_path() in main.cpp).
  char raw[2048] = {};
  XPLMGetSystemPath(raw);
  std::string p(raw);
  if (!p.empty() && p.back() != '/')
    p += '/';
  return p;
}

} // namespace deps
