# Architektur-Landkarte

## Module (`src/`)
| Verzeichnis | Zweck |
| --- | --- |
| `airports/` | `apt.dat`-Parser + DACH-Filter, In-Memory-DB, LLM-Difficulty-Scoring + JSON-Cache (`airport_scores.json`), Geo-Helfer |
| `backends/` | `ILanguageModel`-Interface + `OpenAiLm`/`MistralLm`, async Worker-Threads, `loader.cpp` wählt Provider über `backend_mode` |
| `preflight/` | Regelbasierte Flugvorschlag-Logik (`flight_suggester`), Ground-Control-Filter |
| `postflight/` | Discovery + Parser für ATC-/Flugdaten-JSON, Zeitkorrelation (`ts` ↔ `track[].t`), LLM-Evaluator, Report-Cache (`session_reports.json`) |
| `dependencies/` | Erkennung der externen Plugins (`xp_wellys_devfr_atc`, `xp_pilot`) über `IPluginProbe`, Predicate `deps::all_ready()` |
| `persistence/` | `settings.json`-I/O, Keychain/Credential-Manager-Wrapper |
| `fms/` | FMS-Flugplan-Übernahme (XPLM410 Multi-FPL-API) |
| `ui/` | ImGui-Fenster (Pre-/Post-Flight-Tabs), Clipboard (`.mm` macOS / `_win.cpp` Windows) |
| `core/` | Logging-Sink → `XPLMDebugString` |

## LLM-Einsatz — nur zwei Stellen
1. **Airport-Difficulty-Score** — einmalig pro Airport, gecacht
   (`airport_score_cache`, Invalidierung über `prompt_version` + `input_sig`)
2. **Post-Flight-Bewertung** — pro Session, nach Zeitkorrelation der beiden
   JSONs

Kein Live-LLM für Airport-Suche/-Filterung (rein regelbasiert über `apt.dat`).

## Caching-Entscheidung
JSON statt SQLite für `airport_scores.json` / `session_reports.json` —
Begründung in `docs/airport_score_cache.md` (Datenvolumen klein,
write-once/read-often, keine neue Build-Abhängigkeit).

## Abweichungen vom Referenz-Template (`../xp_wellys_vfr_atc`)
1. **Ein Universal-Build** statt zwei Stages + `lipo` — Trainer ist
   Cloud-only, daher architektur-neutral (`CMAKE_OSX_ARCHITECTURES=
   "arm64;x86_64"` reicht)
2. **Keine lokale Inferenz** (kein whisper.cpp/llama.cpp/Piper) — LLM nur
   als Cloud-Call für die zwei Use Cases oben
3. **Backends nur LM**, kein STT/TTS — Audio-Pipeline gehört ins ATC-Plugin

## Feature-Gating
`deps::all_ready()` (`src/dependencies/`) entscheidet, ob Post-Flight-Reporting
und Session-Scoring in der UI verfügbar sind — beide externen Plugins müssen
installiert UND aktiviert sein. Airport-Suche + FMS-Übernahme bleiben
standalone nutzbar.
