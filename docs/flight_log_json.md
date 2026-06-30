# Flight log JSON

Schema of the flight log exported by the **`xp_pilot`** plugin. This is the
flight-execution input for the post-flight evaluation (position / altitude /
landing quality side of the analysis).

- **Producer:** `xp_pilot` (`src/flight_logger.cpp`)
- **Real sample:** [`testdata/2026-06-30_EDTZ_EDNY_BN2P.json`](../testdata/2026-06-30_EDTZ_EDNY_BN2P.json)
- **Format:** UTF-8 JSON, compact (`nlohmann::json::dump()`).

## Export mechanics

| Aspect | Detail |
| --- | --- |
| Target dir | `<X-Plane>/Output/x_pilot_reports/flights` |
| File name | `YYYY-MM-DD_<DEP>_<ARR>_<ICAO>.json` (date from `gmtime`; `ZZZZ`/`UNKN` as fallbacks; `start_time` appended on collision) |
| Write | `std::ofstream` + `dump()` |
| Trigger | State machine reaches **Shutdown**: landed **and** groundspeed < 5 kts **and** the aircraft profile's shutdown condition (engines off / nav-light off / beacon off). Then `finalize_flight()` writes the file. |

Source: `xp_pilot/src/flight_logger.cpp` (`finalize_flight` write path, state
machine), `xp_pilot/src/html_report.hpp` (track/landing field structs).

## Timestamp format

All epoch fields are **Unix epoch seconds, UTC** from `std::time(nullptr)`
(system clock) — the **same clock source** as the ATC plugin uses internally.

| Field | Format | Source |
| --- | --- | --- |
| `start_time` / `end_time` / `track[].t` / `landings[].time` | epoch seconds (UTC) | `std::time(nullptr)` |
| `start_utc` / `end_utc` | `HH:MM` (UTC) | `gmtime` + `strftime("%H:%M")` |
| `date` | `YYYY-MM-DD` (UTC) | `gmtime` of `start_time` |

See [`post_flight_correlation.md`](./post_flight_correlation.md) for aligning
these against the ATC plugin's local-time strings.

## Top-level structure (17 keys)

| Field | Type | Unit / notes |
| --- | --- | --- |
| `version` | int | Schema version. |
| `date` | string | `YYYY-MM-DD`, UTC. |
| `departure_icao` | string | ICAO, `ZZZZ` if unknown. |
| `arrival_icao` | string | ICAO, `ZZZZ` if unknown. |
| `aircraft_category` | string | e.g. `fixed_wing`, `rotorcraft`. |
| `aircraft_icao` | string | ICAO type, `UNKN` if unknown. |
| `aircraft_tail` | string | Tail/registration (`sim/aircraft/view/acf_tailnum`). |
| `start_time` | int | Epoch s — Idle→Rolling (or Idle→Airborne for rotorcraft). |
| `end_time` | int | Epoch s — Landed→Shutdown. |
| `start_utc` | string | `HH:MM` UTC. |
| `end_utc` | string | `HH:MM` UTC. |
| `block_time_min` | int | `(end_time − start_time) / 60`. |
| `fuel_used_kg` | int | **Currently hardcoded `0` — not implemented.** |
| `max_altitude_ft` | int | ft MSL, flight maximum. |
| `max_speed_kts` | int | kts, flight maximum. |
| `landings` | array | One entry per touchdown. |
| `track` | array | Position samples (~10 s interval). |

## `track[]`

Sampled every **10 s** while logging is enabled (`now − last < 10 → skip`).

| Field | Type | Unit | Source |
| --- | --- | --- | --- |
| `t` | int | epoch s (UTC) | `std::time(nullptr)` |
| `lat` | float | decimal degrees | `sim/flightmodel/position/latitude` |
| `lon` | float | decimal degrees | `sim/flightmodel/position/longitude` |
| `alt` | int | **ft MSL** | `position/elevation` × 3.28084 |
| `spd` | int | **kts IAS** (indicated, not ground speed) | `position/indicated_airspeed` × 1.94384 |
| `vs` | int | fpm | `position/vh_ind_fpm` |

> ⚠️ `spd` is **indicated airspeed**, not ground speed — relevant if the trainer
> ever cross-checks track-derived speeds against GPS distance.

## `landings[]`

| Field | Type | Unit / notes |
| --- | --- | --- |
| `time` | int | epoch s (UTC), nose-gear touchdown. |
| `rating` | string | `BUTTER!` \| `GREAT LANDING!` \| `ACCEPTABLE` \| `HARD LANDING!` \| `WASTED!` (by effective fpm vs. aircraft-profile thresholds, crosswind-adjusted). |
| `fpm` | float | Touchdown vertical speed (fpm; negative = descending). |
| `g_force` | float | Average normal G (30-sample window). |
| `agl_ft` | float | AGL at touchdown (ft). |
| `bounce_count` | int | Main-gear touchdowns before nose-gear. |
| `float_time` | float | s, from flare start (AGL ≤ 15 ft) to nose-gear touchdown. |
| `flare` | string | Flare quality, e.g. `"Good, but late flare"`. |
| `pitch_deg` | float | Pitch at touchdown (deg). |
| `pitch_rate` | float | Pitch rate at touchdown (deg/s). |
| `crosswind_kts` | int | Crosswind magnitude (kts). |
| `crosswind_side` | string | `L` \| `R`. |
| `headwind_kts` | int | Headwind component (+ = headwind, kts). |
| `wind_dir_mag` | int | Wind direction (deg magnetic). |
| `wind_speed_kts` | int | Wind speed (kts). |
| `wind_status` | string | `CALM` (< 3 kts) \| `LIGHT` (3–6) \| `STEADY` (≥ 6). |
| `is_rotorcraft` | bool | Aircraft type. |
