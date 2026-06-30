/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "dependencies/dependencies.hpp"

#include <filesystem>

namespace deps {
namespace {

// The two producers, with the fixed output subdirectory each writes under
// <X-Plane>/Output/. Neither plugin exposes its path, but both derive it from
// XPLMGetSystemPath() + a hardcoded subdir, so the trainer resolves the same
// way. (Signatures and subdirs verified against the source repos.)
struct Descriptor {
  const char *name;
  const char *signature;
  const char *output_subdir; // relative to the X-Plane root
};

constexpr Descriptor kDependencies[] = {
    {"ATC plugin (xp_wellys_vfr_atc)", "ch.thWelly.wellys_devfr_atc",
     "Output/xp_wellys_devfr_atc/flightlog"},
    {"Flight data (xp_pilot)", "thWelly.xp_pilot",
     "Output/x_pilot_reports/flights"},
};

} // namespace

std::vector<DependencyState> evaluate(IPluginProbe &probe) {
  const std::string root = probe.xplane_root();

  std::vector<DependencyState> out;
  out.reserve(std::size(kDependencies));
  for (const auto &d : kDependencies) {
    const PluginStatus st = probe.probe(d.signature);
    DependencyState s;
    s.name = d.name;
    s.signature = d.signature;
    s.output_path = root + d.output_subdir;
    s.installed = st.installed;
    s.enabled = st.enabled;
    std::error_code ec; // non-throwing; missing path simply yields false
    s.output_dir_exists = std::filesystem::exists(s.output_path, ec);
    out.push_back(std::move(s));
  }
  return out;
}

bool all_ready(const std::vector<DependencyState> &states) {
  if (states.empty())
    return false;
  for (const auto &s : states)
    if (!s.installed || !s.enabled)
      return false;
  return true;
}

} // namespace deps
