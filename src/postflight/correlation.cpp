/*
 * xp_wellys_vfr_trainer - VFR training gamification layer for X-Plane 12
 * Copyright (C) 2026 thWelly & Claude (Anthropic)
 *
 * Licensed under the GNU GPL-3.0-or-later. See LICENSE.
 */

#include "postflight/correlation.hpp"

#include <cstdlib>

namespace postflight {

namespace {

// Nearest track point to `ts` by absolute epoch distance. Track is time-ordered
// but a linear scan is fine (a flight has ~hundreds of samples). Returns nullptr
// only when the track is empty.
const TrackPoint *nearest(const std::vector<TrackPoint> &track,
                          std::int64_t ts, std::int64_t &best_dt) {
  const TrackPoint *best = nullptr;
  for (const auto &p : track) {
    const std::int64_t d = std::llabs(p.t - ts);
    if (best == nullptr || d < best_dt) {
      best = &p;
      best_dt = d;
    }
  }
  return best;
}

} // namespace

CorrelatedTimeline correlate(const AtcLog &atc, const FlightLog &flight,
                             std::int64_t tolerance_s) {
  CorrelatedTimeline out;
  out.events.reserve(atc.transmissions.size());

  for (const auto &tx : atc.transmissions) {
    CorrelatedEvent ev;
    ev.tx = tx;
    std::int64_t dt = 0;
    if (const TrackPoint *p = nearest(flight.track, tx.ts, dt);
        p != nullptr && dt <= tolerance_s) {
      ev.has_position = true;
      ev.track = *p;
      ev.dt = dt;
    }
    out.events.push_back(std::move(ev));
  }

  return out;
}

} // namespace postflight
