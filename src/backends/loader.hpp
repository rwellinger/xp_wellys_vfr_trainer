/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef BACKENDS_LOADER_HPP
#define BACKENDS_LOADER_HPP

namespace backends::loader {

// Read settings::backend_mode(), load the matching API key from the Keychain,
// and register the concrete language model with the manager. Cloud-only, so
// this is synchronous and cheap (no model download / hash verification).
// A missing API key logs an error and leaves no LM registered — lm_ready()
// stays false and the UI surfaces "no key configured".
void start();

// Drop any registered backend. Idempotent.
void stop();

} // namespace backends::loader

#endif // BACKENDS_LOADER_HPP
