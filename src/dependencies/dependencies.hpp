/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef DEPENDENCIES_DEPENDENCIES_HPP
#define DEPENDENCIES_DEPENDENCIES_HPP

#include <string>
#include <vector>

// Detection of the two external plugins the post-flight reporting depends on:
//   - xp_wellys_vfr_atc (ATC transmission JSON)
//   - xp_pilot          (flight data JSON)
//
// Airport search and FMS loading work standalone; only reporting / session
// scoring require both plugins. The X-Plane SDK calls are abstracted behind
// IPluginProbe so the resolution logic lives in the SDK-free engine library
// and is unit-testable with a fake probe (mirrors backends::ILanguageModel).
namespace deps {

// Result of probing one plugin by signature. enabled is only meaningful when
// installed is true.
struct PluginStatus {
  bool installed = false;
  bool enabled = false;
};

// Abstracts the X-Plane plugin/system-path API so evaluate() can be tested
// without the SDK.
class IPluginProbe {
public:
  virtual ~IPluginProbe() = default;

  // Look up a plugin by its registration signature.
  virtual PluginStatus probe(const std::string &signature) = 0;

  // X-Plane root folder as a POSIX path with a trailing '/'.
  virtual std::string xplane_root() = 0;
};

// Resolved state of one dependency.
struct DependencyState {
  std::string name;      // human-readable display name
  std::string signature; // XPLM registration signature
  std::string output_path;        // <root>Output/<subdir> where its JSON lands
  bool installed = false;
  bool enabled = false;
  bool output_dir_exists = false; // filesystem check, informational only
};

// Evaluate every known dependency against the probe (one entry per dependency,
// in a stable order).
std::vector<DependencyState> evaluate(IPluginProbe &probe);

// True only when every dependency is installed AND enabled — the gate for
// post-flight reporting and session "GO" scoring (#6 / #7).
bool all_ready(const std::vector<DependencyState> &states);

} // namespace deps

#endif
