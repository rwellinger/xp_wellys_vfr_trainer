# Airport difficulty score cache — decision & schema

Resolves open question #4 (`CLAUDE.md`). Airport difficulty scores are computed
**once** per airport via the LLM and **cached** (no live LLM during airport
selection — see Nicht-Ziele in `CLAUDE.md`). This records the storage decision
and sketches the format; the implementation belongs to the scoring work (#5).

## Decision: JSON (not SQLite)

A single JSON file at **`<plugin>/data/airport_scores.json`** (via
`settings::get_data_dir()`, `src/persistence/settings.hpp:21`), serialised with
the already-vendored nlohmann library (`vendor/json.hpp`).

### Rationale
| Kriterium | JSON | SQLite |
| --- | --- | --- |
| Build-Abhängigkeit | **keine** (nlohmann schon vendored, von `settings.cpp` genutzt) | neue Abhängigkeit (`libsqlite3` / Amalgamation) |
| Datenvolumen | ~819 DACH-Airports, write-once / read-often → trivial | Stärken (Queries, Transaktionen, Nebenläufigkeit) greifen erst viel größer |
| Zugriffsmuster | Score wird einmal berechnet, danach nur gelesen → komplettes Laden/Schreiben reicht | Overkill für volle In-Memory-Menge |
| Inspizierbarkeit | menschenlesbar, diff-bar, manuell editierbar | Binär, braucht Tooling |
| Konsistenz mit Projekt | folgt dem etablierten `settings.json`-Muster (atomarer temp+rename-Write) | neues Paradigma |

Bei dieser Größenordnung wäre SQLite Over-Engineering (vgl. Coding-Regel „kein
Over-Engineering" in `CLAUDE.md`). Sollte sich der Scope drastisch ändern (z. B.
globaler Datensatz statt DACH, häufige nebenläufige Writes), ist eine Migration
zu SQLite jederzeit möglich — das File-`version`-Feld (s. u.) deckt das ab.

## Schema (Skizze)

```json
{
  "version": 1,
  "scores": {
    "EDDS": {
      "score": 7,
      "prompt_version": 3,
      "input_sig": "3f9a1c2d",
      "model": "openai:gpt-4o-mini",
      "computed_at": "2026-06-30T11:35:43Z",
      "rationale": "Class C, busy parallel ops, ATIS + Director"
    },
    "EDFT": {
      "score": 3,
      "prompt_version": 3,
      "input_sig": "b7e40a11",
      "model": "openai:gpt-4o-mini",
      "computed_at": "2026-06-30T11:40:02Z",
      "rationale": "AFIS field, single runway, low traffic"
    }
  }
}
```

| Feld | Typ | Bedeutung |
| --- | --- | --- |
| `version` | int | **File-Schema**-Version. Mismatch → ganze Datei verwerfen / migrieren. |
| `scores` | object | ICAO → Score-Eintrag. |
| `scores.<ICAO>.score` | int | Schwierigkeits-Score (Skala in #5 festzulegen, z. B. 1–10). |
| `scores.<ICAO>.prompt_version` | int | Version des Scoring-Prompts, der diesen Score erzeugt hat. |
| `scores.<ICAO>.input_sig` | string | Stabile Signatur (Hash) über die scoring-relevanten apt.dat-Eingaben. |
| `scores.<ICAO>.model` | string | Provider+Modell (Nachvollziehbarkeit, nicht für Invalidierung). |
| `scores.<ICAO>.computed_at` | string | ISO-8601-UTC-Zeitstempel der Berechnung. |
| `scores.<ICAO>.rationale` | string | Optionale Kurzbegründung des LLM (Debug/UI). |

`prompt_version` und `input_sig` liegen **pro Eintrag** (nicht global), damit
inkrementell neu berechnet werden kann — nur veraltete Einträge, nicht der ganze
Cache.

## Invalidierung

Beim Nachschlagen eines Airports `A` mit aktueller Prompt-Version `P` und frisch
berechneter `S = input_sig(A)`:

1. Eintrag fehlt → **berechnen**.
2. `entry.prompt_version != P` → **neu berechnen** (Prompt geändert).
3. `entry.input_sig != S` → **neu berechnen** (apt.dat-Eingaben geändert,
   z. B. neue Runway/Frequenz/Facility-Typ).
4. sonst → **Cache verwenden**.

Zusätzlich: File-`version`-Mismatch → gesamte Datei ignorieren/migrieren.

### `input_sig` — was hineinfließt
Eine kurze, stabile Signatur (z. B. FNV-1a-Hash, hex) über **exakt die
Airport-Felder, die der Scoring-Prompt konsumiert** — Single Source of Truth,
damit jede relevante apt.dat-Änderung den Score invalidiert. Kandidatenfelder
(final in #5 festzulegen, passend zum Prompt):
`facility`, Runway-Anzahl, max. Runway-Länge, Surface-Codes,
Frequenz-Typen/-Anzahl, `elevation_ft`. **Nicht** einbeziehen, was den Score
nicht beeinflusst (z. B. `name`, exakte `rationale`).

> Wichtig: Die Signatur-Funktion muss neben dem Scoring-Code (#5) leben und genau
> die Prompt-Eingaben spiegeln. Weicht sie ab, wird entweder zu oft (harmlos,
> aber teuer) oder zu selten (falsche stale Scores) neu berechnet.

## Implementierungs-Hinweise (für #5)
- Cache-Modul SDK-frei in `src/airports/` (z. B. `airport_score_cache.{hpp,cpp}`),
  Pfad von der Plugin-Seite über `settings::get_data_dir()` injiziert — analog zur
  Trennung im apt.dat-Parser (#3).
- Atomarer Write (temp + `std::rename`), wie `settings.cpp` /
  `cross_country_log.cpp` im ATC-Template.
- LLM-Aufruf läuft bereits async (`backends::respond_json_async`); Cache-Write
  klein genug, um nach dem Callback auf dem Hauptthread zu erfolgen.
