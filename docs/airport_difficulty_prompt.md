# Airport difficulty scoring — prompt & schema (#5)

The difficulty score shown in the pre-flight table comes from a one-time LLM
rating per airport, cached in `airport_scores.json`
(`docs/airport_score_cache.md`). This documents the reproducible prompt and the
response schema. The source of truth is the code
(`src/airports/airport_scorer.cpp`); keep this file in sync.

- **Prompt version:** `kPromptVersion = 1` (bump on any prompt/rubric change so
  cached scores recompute).
- **Scale:** integer **1–10** (consistent with `provisional_difficulty()` and
  `difficulty_bucket()`: EASY 1–3, MEDIUM 4–6, HARD 7–10).
- **Transport:** `backends::lm::respond_json_async` (JSON-object reply).
- **Trigger:** on-search. When the user requests suggestions, every GA field
  **in range** of the departure (ignoring difficulty) is enqueued and scored in
  the background, then cached. A difficulty filter only trusts real LLM scores
  (`candidates_in_range()` + the provisional-exclusion in `suggest_flights()`),
  so results appear as scores land.
- **Batching:** up to `kBatchSize = 30` airports per LLM call (the manager
  serialises inferences, so fewer calls = faster). One single call for all
  airports is avoided — it would exceed the output-token limit and degrade
  quality.

## Response schema (strict, batched)
```json
{ "scores": [ { "icao": "LOWI", "score": 7, "rationale": "one short sentence" } ] }
```
- One entry per requested airfield, keyed by `icao`.
- `score` — integer 1–10 (clamped on parse).
- `rationale` — short justification, shown as a tooltip; never used for
  invalidation.

Entries are cached per ICAO. A reply that is not parseable is discarded (no
silent wrong score); any airfield missing from the reply stays uncached and is
re-enqueued on the next search.

## System prompt
```
You are a VFR flight instructor familiar with general-aviation flying in the
DACH region (Germany, Switzerland, Austria). Rate how demanding a given airfield
is for a VFR pilot on a 1-10 scale: 1 = very easy (quiet AFIS/uncontrolled
field, flat terrain, long runway), 10 = very demanding (busy controlled airspace
and/or mountainous, high-elevation, short or tricky runway).
Weigh: controlled (Tower) vs. AFIS workload; surrounding terrain and elevation
(mountains, valley approaches); nearby controlled airspace (CTR/TMA) and traffic
complexity; runway length, surface and count.
Base the rating primarily on the facts provided. You may use general geographic
knowledge of the SPECIFIC airfield named to judge terrain and airspace, but do
NOT invent specific frequencies, procedures or figures; when unsure, stay
conservative.
Respond with ONLY a JSON object, no prose:
{"score": <integer 1-10>, "rationale": "<one short sentence>"}
```

## User content (facts from apt.dat)
Structured facts for the airport being scored, e.g.:
```
ICAO: LOWI
Name: Innsbruck
Position: 47.2602, 11.3439
Elevation: 1907 ft
Facility: TOWERED
Runways:
  - 2000 m, surface code 1
Frequencies:
  - TOWER 120.100 MHz
  - ATIS 126.025 MHz
```

## Cache invalidation input (`input_sig`)
`input_sig(Airport)` (FNV-1a, hex) hashes exactly the scoring-relevant fields so
any change that would change the rating recomputes the score:
`facility`, runway count, max runway length (rounded m), sorted surface codes,
sorted frequency types, `elevation_ft` (rounded). It deliberately excludes
`name` and exact coordinates (stable per ICAO). It must mirror the facts the
prompt consumes — see the note in `airport_scorer.cpp`.
