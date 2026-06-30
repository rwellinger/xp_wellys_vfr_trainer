# ATC Transmission JSON

Schema of the ATC transmission report exported by the **`xp_wellys_vfr_atc`**
plugin. This is the primary input for the post-flight evaluation (intent /
phraseology side of the analysis).

- **Producer:** `xp_wellys_vfr_atc`
- **Real sample:** [`testdata/2026-06-30_1135_EDTZ-EDNY.json`](../testdata/2026-06-30_1135_EDTZ-EDNY.json)
- **Format:** UTF-8 JSON, pretty-printed (2-space indent), key order preserved
  (`nlohmann::ordered_json`).

## Export mechanics

| Aspect | Detail |
| --- | --- |
| Target dir | `<X-Plane>/Output/xp_wellys_devfr_atc/flightlog` |
| File name | `YYYY-MM-DD_HHMM_<DEP>[-<DEST>].json` (timestamp frozen at flight open) |
| Write | Atomic: temp file + `std::rename` |
| Trigger | Automatic after **every** transmission (`atc/engine.cpp:392` → `cross_country_log::write`) |
| New flight (split) | Departure-type initial call while IDLE + on ground after an airborne flight; X-Plane reposition (`AIRPORT_LOADED` / `PLANE_LOADED`); manual "New Flight" button |
| Rename to `-<DEST>` | First `INITIAL_CALL_INBOUND` / `..._VRP` to a different airport |

Source references point into `../xp_wellys_vfr_atc` (template/source plugin):
`core/cross_country_log.cpp` (path/filename/flush/split), `atc/engine.cpp`
(per-transmission write, failure-locus heuristic).

## Timestamp format

`time` (per transmission) and `flight.started_at` use
**`YYYY-MM-DDTHH:MM:SS` — ISO 8601 local time, no timezone suffix**.

- Source: system clock (`std::time`) converted via `localtime_r`
  (`core/cross_country_log.cpp:56-68`). **Not UTC.**
- All timestamps for one flight are **frozen at `open_flight()`** — they reflect
  the moment the flight file was opened, not each individual transmission's
  wall-clock moment.

> ⚠️ This is the crux for correlating with the xp_pilot flight log, which uses
> Unix epoch + UTC. See [`post_flight_correlation.md`](./post_flight_correlation.md).

## Top-level structure

```json
{
  "version": 1,
  "flight": { ... },
  "summary": { ... },
  "transmissions": [ { ... }, ... ]
}
```

| Field | Type | Notes |
| --- | --- | --- |
| `version` | int | Schema version (currently `1`). |
| `flight` | object | Flight header, see below. |
| `summary` | object | Aggregate counters, see below. |
| `transmissions` | array | One object per radio call, see below. |

### `flight`

| Field | Type | Notes |
| --- | --- | --- |
| `started_at` | string | ISO 8601 local time, frozen at flight open. |
| `departure_airport` | string | ICAO. |
| `destination_airport` | string\|null | ICAO; set once an inbound call to a different field is seen. |
| `missing_initial_call` | bool | `true` if a controlled field (Tower/Ground/AFIS) was left airborne without an initial call. Only measured at controlled fields. |
| `pilot_callsign` | string | Spelled-out callsign, e.g. `"Delta Tango Whiskey Romeo Oscar"`. |

### `summary`

| Field | Type | Notes |
| --- | --- | --- |
| `transmissions` | int | Total transmission count. |
| `classified` | int | Count with `outcome == "classified"`. |
| `unknown` | int | Count with `outcome == "unknown"`. |
| `garbled` | int | Count with `outcome == "tower_reported_garbled"`. |
| `lm_fallbacks` | int | Count routed through the language model (`path == "lm_fallback"`). |
| `readback_issues` | int | Count of readbacks with missing elements. |
| `phases` | array<string> | Distinct `flight_phase` values seen this flight. |

### `transmissions[]`

| Field | Type | Notes |
| --- | --- | --- |
| `time` | string | ISO 8601 local time (frozen at flight open — see above). |
| `transcript` | string | Whisper transcription of the pilot's call. |
| `quality` | float | Whisper transcription quality 0.0–1.0. `< 0.3` → rejected / "say again". |
| `atc_response` | string | ATC reply text; empty string when no reply (e.g. pure readback). |
| `intent` | string | Classified pilot intent (see enum below). |
| `confidence` | float | Rule-parser confidence 0.0–1.0. `≥ 0.7` → skip LM; `≥ 0.9` → readback safety net. |
| `path` | string | Classification path: `rule_skip_lm` \| `lm_fallback` \| `clearance_match`. |
| `lm_used` | bool | Whether the language model was invoked. |
| `lm_backend` | string\|null | `"openai"` / `"mistral"` when used, else `null`. |
| `lm_ready` | bool\|null | LM readiness at decision time, else `null`. |
| `outcome` | string | `classified` \| `unknown` \| `tower_reported_garbled`. |
| `state` | string | ATC state machine state, e.g. `IDLE`, `Pattern/PATTERN_ENTRY`, `Pattern/LANDING_CLEARED`. |
| `flight_phase` | string | Aircraft motion phase (see enum below). |
| `expected_intent` | string | CSV of valid intents for the current state (volatile, not a fixed enum). |
| `vrp_name_set` | bool | Whether a VRP (visual reporting point) name was set. |
| `vrp_name` | string\|null | VRP name when set, else `null`. |
| `readback_missing_elements` | array\|null | Missing readback elements (`[]` = readback complete, `null` = not a readback). |
| `failure_locus` | string\|null | Offline failure hint: `phraseology` \| `proper_name` \| `mixed` \| `unclear`. Emitted only when `outcome != "classified"` **or** `lm_used`. |

## Enumerations

**`path`** — classification route
- `rule_skip_lm` — high-confidence rule parser match, or LM not ready.
- `lm_fallback` — low-confidence rule (`< 0.7`) → LM invoked.
- `clearance_match` — deterministic readback recognition against a prior clearance.

**`outcome`** — final classification result
- `classified` — a valid intent was recognised.
- `unknown` — no recognisable elements.
- `tower_reported_garbled` — some ATC elements recognised but unclear.

**`flight_phase`** (`atc/flight_phase.hpp`)
`PARKED`, `TAXI`, `TAKEOFF_ROLL`, `CLIMB`, `PATTERN`, `FINAL_APPROACH`,
`LANDING_ROLL`, `CRUISE`.
Airborne phases: everything except `PARKED` / `TAXI`.

**`state`** (`atc/atc_state_machine.hpp`)
`IDLE`, `GROUND_CONTACT`, `TAXI_CLEARED`, `TOWER_CONTACT`, `DEPARTURE_CLEARED`,
`PATTERN_ENTRY`, `LANDING_CLEARED`, `TOUCH_AND_GO_CLEARED`, `UNICOM_ACTIVE`,
`EN_ROUTE`. Serialised as a path (e.g. `Pattern/PATTERN_ENTRY`).

**`failure_locus`** (`atc/engine.cpp` `guess_failure_locus`)
- `phraseology` — German phraseology keywords detected.
- `proper_name` — VRP name detected.
- `mixed` — both detected.
- `unclear` — neither detected.

**`intent`** — pilot intent (`atc/intent_parser.hpp`, 23 values)
`UNKNOWN`, `INITIAL_CALL_GROUND`, `INITIAL_CALL_INBOUND`,
`INITIAL_CALL_INBOUND_VRP`, `REQUEST_TAXI`, `REQUEST_TAXI_PARKING`,
`READY_FOR_DEPARTURE`, `READY_FOR_DEPARTURE_VFR`, `READBACK`,
`REPORT_POSITION_DOWNWIND`, `REPORT_POSITION_BASE`, `REPORT_POSITION_FINAL`,
`REQUEST_LANDING`, `REQUEST_TOUCH_AND_GO`, `GO_AROUND`, `RUNWAY_VACATED`,
`RUNWAY_VACATED_TOWER_ONLY`, `LEAVING_FREQUENCY`, `RADIO_CHECK`, `SELF_ANNOUNCE`,
… (the values above are those observed in the sample plus the parser enum;
treat the consumer as tolerant of unknown intent strings).

> Note: `intent` / `expected_intent` strings are not frozen as a contract — new
> intents may appear. The trainer should treat unrecognised values as opaque
> rather than failing.
