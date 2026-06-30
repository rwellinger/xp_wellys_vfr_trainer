# Post-flight correlation: ATC ↔ flight log

The post-flight evaluation needs to align two independently produced JSON files
(both real samples live under [`testdata/`](../testdata)):

- **ATC transmissions** — [`atc_transmission_json.md`](./atc_transmission_json.md)
  (producer `xp_wellys_vfr_atc`).
- **Flight log** — [`flight_log_json.md`](./flight_log_json.md)
  (producer `xp_pilot`).

## The core problem: two incompatible time systems

| Source | Time fields | Format | Reference |
| --- | --- | --- | --- |
| ATC | `time`, `flight.started_at` | `YYYY-MM-DDTHH:MM:SS` | **Local time, no timezone**, from system clock via `localtime_r` |
| Flight log | `track[].t`, `start_time`, `end_time` | integer | **Unix epoch seconds (UTC)** |
| Flight log | `start_utc`, `end_utc` | `HH:MM` | **UTC** wall clock |

The two never share a directly comparable field. The ATC timestamps carry **no
timezone**, while the flight log is pure UTC epoch. They can only be matched by
resolving the local-vs-UTC offset.

### Sample evidence (EDTZ→EDNY, 2026-06-30)

- ATC first transmission `time`: `11:35:43` (local).
- Flight log `start_utc`: `09:40`, `start_time`: `1782812450`.
- Offset ≈ **+2h** → consistent with **CEST (UTC+2)** in the DACH region in summer.

So `11:35 local − 2h = 09:35 UTC`, which sits right next to the flight log's
`09:40` start — the two files describe the same flight.

## Cleanest fix: emit epoch in the ATC plugin (recommended)

Both plugins are first-party, so the mismatch is best removed at the source —
and the simpler side to change is **`xp_wellys_vfr_atc`**, not `xp_pilot`:

- `xp_pilot` already uses the **canonical, unambiguous** format (Unix epoch UTC).
  Changing it to local time would *introduce* ambiguity — wrong direction.
- `xp_wellys_vfr_atc` already has the epoch in hand: `now_parts()` computes
  `std::time(nullptr)` (`core/cross_country_log.cpp:57`) — the *same* system-clock
  epoch xp_pilot writes into `track[].t` — and then discards it via `localtime_r`.

**Minimal, backward-compatible change in the ATC plugin:** keep that epoch and add
an integer field per transmission (e.g. `ts`) plus `started_at_epoch` in `flight`,
next to the existing local-time strings (`entry_to_json` / `open_flight`,
~3–4 lines). Correlation then becomes a **direct integer comparison** against
`track[].t` — no timezone parsing, no DST handling, no per-flight offset anchor.

Until that lands, the trainer must do the local→UTC reconstruction below.

## Recommended alignment approach (until the producer emits epoch)

1. **Normalise ATC timestamps to epoch-UTC.** Parse the naive local timestamp,
   then apply the offset. Options, in order of robustness:
   - Re-derive the offset empirically per flight by anchoring the ATC
     `started_at` against the flight log `start_time` / `start_utc` (no fixed TZ
     assumption — survives any DACH timezone and DST). **Preferred.**
   - Fall back to the system timezone of the recording machine only if no flight
     log is present.
2. **Match transmissions to track points by time window.** For each transmission,
   find the nearest `track[].t` (track sampling is ~10 s; see flight log doc) and
   attach position/altitude/speed. A tolerance of ±(sampling interval) is enough.
3. **Feed the joined timeline to the LLM** for the post-flight judgement
   (phraseology + flight execution combined).

## Open risks / caveats

- **No timezone in ATC JSON.** A fixed UTC+2 assumption breaks for winter flights
  (CET, UTC+1), non-DACH system clocks, or a misconfigured sim machine. Always
  prefer the empirical offset derived from the flight-log anchor.
- **DST boundaries.** Flights spanning the DST switch are a corner case; the
  empirical per-flight offset avoids most of it but not a mid-flight switch.
- **Same clock source.** Both producers call `std::time(nullptr)`, so once the
  ATC local time is converted back to epoch they share one clock — provided both
  files come from the same machine/session (true for a normal sim recording).
  Only `flight.started_at` and the ATC file name are frozen at flight open;
  per-transmission `time` values *are* live wall-clock samples.
- **Track speed is IAS.** `track[].spd` is indicated airspeed, not ground speed —
  do not use it to validate positions against GPS-derived distances.
