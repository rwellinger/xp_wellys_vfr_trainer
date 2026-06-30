/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef DEPENDENCIES_XPLM_PLUGIN_PROBE_HPP
#define DEPENDENCIES_XPLM_PLUGIN_PROBE_HPP

#include "dependencies/dependencies.hpp"

namespace deps {

// IPluginProbe backed by the live X-Plane SDK. Lives in the plugin module
// (XPLM-dependent); tests use a fake instead.
class XplmPluginProbe final : public IPluginProbe {
public:
  PluginStatus probe(const std::string &signature) override;
  std::string xplane_root() override;
};

} // namespace deps

#endif
