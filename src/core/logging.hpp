/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef LOGGING_HPP
#define LOGGING_HPP

namespace logging {

using Sink = void (*)(const char *);

// Plugin installs an XPLMDebugString wrapper in XPluginStart. Default sink
// writes to stderr so the engine module is usable from a headless test
// client before any sink is installed.
void set_sink(Sink s);

void debug(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

} // namespace logging

#endif
