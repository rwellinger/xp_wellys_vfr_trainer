# Post-flight report: prompt + JSON schema

The post-flight evaluation (#6) judges one flown session with the LLM and
persists the result. Implementation: [`src/postflight/evaluator.cpp`](../src/postflight/evaluator.cpp)
(`kSystemPrompt`, `build_prompt`, `evaluate_async`).

## Inputs

1. **ATC transmission JSON** (v2) — [`atc_transmission_json.md`](./atc_transmission_json.md).
2. **Flight log JSON** — [`flight_log_json.md`](./flight_log_json.md).

The two are time-correlated (`src/postflight/correlation.cpp`) into a single
timeline: each radio transmission (`ts`) is joined to the nearest flight-log
track point (`track[].t`) within ±15 s, attaching altitude / speed. See
[`post_flight_correlation.md`](./post_flight_correlation.md).

## User prompt (built from the correlated timeline)

`build_prompt()` renders a compact, model-friendly text:

```
Flight: EDTZ -> EDNY (BN2P)
Block time: 15 min
Max altitude: 2604 ft

Timeline (UTC time | phase | pilot -> controller | position):
[09:42:24 PATTERN] "Konstanz Information ... Verlassen Ihre Frequenz" (LEAVING_FREQUENCY) -> ATC: "... auf Wiederhoeren." @ 2523 ft, 241 kt IAS
...

Landings:
  - rating BUTTER!, -54 fpm, 1.01 G, bounces 0, flare "Good, but late flare", crosswind 1 kt (CALM)
```

## System prompt (rubric)

Two independent **1–10** sub-scores plus a phase-linked detail breakdown
(`kSystemPrompt`):

- **phraseology_score** — radio discipline and standard VFR phraseology:
  correct initial calls, position reports, readbacks, callsign use, no missing
  or garbled elements.
- **execution_score** — flight execution: sensible altitudes and pattern
  flying, stable approach, and the provided landing quality.
- **findings** — the detail part (#21): one entry per concrete point of praise
  or criticism, each tied to the flight phase / radio call it belongs to (from
  the phase + intent shown in the timeline). Empty when nothing is noteworthy;
  capped at ~6 entries.

The model is instructed to judge only from the supplied data (no invented
facts/frequencies), keep rationales short, and reply with JSON only.

### Report language (#21)

`evaluator::system_prompt(language)` appends a directive to `kSystemPrompt` so
the report text (rationales, summary, and each finding's `phase` + `text`) is
written in **German** (`"de"`) or **English** (`"en"`). The language comes from
the `report_language` setting (**default `de`**, Settings tab) and is passed
into `evaluate_async(...)` per call; it is stored on the report for provenance.

## Output JSON schema

```json
{
  "phraseology_score": 8,
  "phraseology_rationale": "Correct initial call and readbacks; ...",
  "execution_score": 7,
  "execution_rationale": "Stable pattern, smooth touchdown; ...",
  "summary": "Solid VFR circuit. Work on ...",
  "findings": [
    {
      "phase": "Initial call",
      "category": "phraseology",
      "sentiment": "critique",
      "text": "Callsign was missing at the end of the first call."
    }
  ]
}
```

Scores are clamped to 1–10 on parse. Findings are parsed defensively: an entry
with empty `text` is dropped, an unknown `category` is cleared, and an unknown
`sentiment` falls back to `critique`. The UI renders the general verdict first
(scores + summary), then the detail findings — only when the list is non-empty.

Each result is stored as its **own file** under
`<X-Plane>/Output/xp_wellys_vfr_trainer/`, named
`<YYYY-MM-DD_HHMM>_<DEP>[-<DEST>]_trainingsreport.json` (timestamp UTC, mirroring
the other plugins' logs). On load the directory is scanned into an in-memory map
keyed by `<departure_icao>_<started_at_epoch>`
([`report_cache.cpp`](../src/postflight/report_cache.cpp)), so re-opening a
scored session shows the cached report without another LLM call. The old
single-file `<plugin>/data/session_reports.json` is no longer used (not
migrated). Bump `evaluator::kPromptVersion` when the rubric changes and
`report_cache::kFileVersion` when the on-disk layout changes.
