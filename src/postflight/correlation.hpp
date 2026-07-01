/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#ifndef POSTFLIGHT_CORRELATION_HPP
#define POSTFLIGHT_CORRELATION_HPP

#include "postflight/atc_log.hpp"
#include "postflight/flight_log.hpp"

#include <cstdint>
#include <vector>

// Time-correlate the two post-flight JSON sources. Both producers stamp their
// timestamps from the same system clock (std::time), so with the ATC plugin's
// v2 UTC-epoch fields the join is a direct integer comparison: attach the
// nearest flight-log track point (by |ts - t|) to each ATC transmission. No
// timezone / DST reconstruction (see docs/post_flight_correlation.md).
namespace postflight {

// Default match tolerance in seconds. Track sampling is ~10 s, so a transmission
// within ±(one sample) of a track point gets a position; anything farther
// (e.g. calls made while parked before the first track sample) stays unmatched.
constexpr std::int64_t kCorrelationToleranceS = 15;

struct CorrelatedEvent {
  Transmission tx;
  bool has_position = false;
  TrackPoint track;       // valid only when has_position is true
  std::int64_t dt = 0;    // |tx.ts - track.t| when matched, else 0
};

struct CorrelatedTimeline {
  std::vector<CorrelatedEvent> events; // one per transmission, source order
};

// Join every transmission to its nearest track point within `tolerance_s`.
// Transmissions are kept even when unmatched (has_position = false).
CorrelatedTimeline correlate(const AtcLog &atc, const FlightLog &flight,
                             std::int64_t tolerance_s = kCorrelationToleranceS);

} // namespace postflight

#endif // POSTFLIGHT_CORRELATION_HPP
