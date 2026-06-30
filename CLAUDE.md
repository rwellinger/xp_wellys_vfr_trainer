# CLAUDE.md – xp_wellys_vfr_trainer

## Projekt-Kontext
X-Plane 12 Plugin für macOS (ARM64, Mac Studio M1).
VFR-Trainer mit Gamification-Layer über das ATC-Plugin.
Ziel: Nutzer zu echten Flügen motivieren, ATC-Training im DACH-Raum.

## Template-Projekt
`../xp_wellys_vfr_atc` ist das Referenz-Plugin.
Vor jeder Implementierung dort nachschauen wie es gelöst ist:
- UI-Patterns
- LLM-Client-Abstraktion
- Makefile-Struktur
- Test-Patterns
- X-Plane SDK-Verwendung

Adaptieren, nicht blind kopieren. Abweichungen begründen.

## Plattform & Toolchain
- macOS ARM64 (Apple Silicon)
- C++ Native X-Plane Plugin
- Miniconda für Python-Hilfsscripts falls nötig
- Colima für Docker falls nötig
- Build via Makefile (siehe `../xp_wellys_vfr_atc/Makefile`)

## Sprache
- Code: Englisch
- Kommentare: Englisch
- Kommunikation mit Entwickler: Deutsch

## Coding-Regeln
- Kurz und präzise – kein Over-Engineering
- Keine Annahmen bei Unklarheiten – fragen
- Faktenbasiert – keine Halluzinierungen bei ICAO-Codes oder API-Details
- Vor neuer Implementierung prüfen ob `../xp_wellys_vfr_atc` es bereits löst

## LLM-Integration
- Provider: Mistral oder OpenAI (Nutzerwahl)
- Abstraktion aus `../xp_wellys_vfr_atc` übernehmen
- LLM nur für:
  - Airport-Schwierigkeits-Score (einmalig, gecacht)
  - Post-Flight Bewertung (pro Session)
- Kein LLM für Airport-Suche oder Filterung

## Airport-Daten
- Quelle: X-Plane `apt.dat` (keine externe DB)
- DACH-Filter: ICAO-Prefixes `ED`, `LS`, `LO`, `ET`, `LZ`
- Caching-Strategie: mit Claude Code klären (SQLite vs. JSON)

## Wichtige Abhängigkeiten
- `xp_wellys_devfr_atc` → liefert ATC Transmission JSON
- `xp_pilot` → liefert Flugdaten JSON (Koordinaten + Zeitstempel)
- Beide JSONs werden post-flight zeitkorreliert

## Nicht-Ziele
- Kein Exam-Zwang für Nutzer
- Keine Flugzeug-Einschränkung
- Kein Live-LLM für Airport-Suche
- Keine externe Airport-Datenbank

---

# Aktueller Stand

_Stand: 2026-06-29_

## Was steht (Grundgerüst)
Voll lauffähiges Plugin-Skelett, vom Template `../xp_wellys_vfr_atc` adaptiert.
`make setup && make build && make test` laufen grün, `make install` deployt ein
ad-hoc-signiertes universal `.xpl`. Details siehe `README.md`.

- **Plugin-Lifecycle** (`src/main.cpp`): `XPluginStart/Stop/Enable/Disable`,
  Flight-Loop (draint nur die LM-Callback-Queue, mit Exception-Boundary), Menü
  „Welly's VFR Trainer" + bindbares Command.
- **LLM-Abstraktion** (`src/backends/`): `ILanguageModel` + `OpenAiLm` /
  `MistralLm`, async Worker-Threads, `respond_async` / `respond_json_async`,
  `drain_callback_queue()`. `loader.cpp` wählt den Provider nach `backend_mode`.
- **Infrastruktur**: `logging` → `XPLMDebugString`, `settings` (JSON in
  `<plugin>/data`), `keychain` (API-Keys im macOS-Keychain, getrennte Services
  je Provider), `trainer_ui` (ImGui-Platzhalterfenster).
- **Tests**: Catch2 gegen die SDK-freie `xp_trainer_engine` OBJECT-Library.

## Plugin-Identität
- Name: `Welly's VFR Trainer v<VERSION>`
- Signatur: `ch.thWelly.wellys_vfr_trainer`
- Keychain-Services: `com.xp_wellys_vfr_trainer.openai` / `.mistral`

## Architektur-Entscheidungen (begründete Abweichungen vom Template)
1. **Universal-Build in einem Schritt** statt zwei Stages + `lipo`. Grund: Der
   Trainer ist reines Cloud-Plugin (keine lokale Inferenz), daher
   architektur-neutral. Ein CMake-Build mit
   `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` reicht.
2. **Keine lokale Inferenz** (kein whisper.cpp / llama.cpp / Piper, keine
   `spikes/`-Submodule). LLM nur als Cloud-Aufruf für die zwei Use Cases.
3. **Backends nur LM** – kein STT/TTS (das Audio-Pipeline-Thema gehört ins
   ATC-Plugin, nicht in den Trainer).

## Build-Kommandos
```
make setup     # SDK + vendor laden (sdk/, vendor/ — nicht versioniert)
make build     # universal .xpl -> build/
make test      # Catch2-Unit-Tests
make install   # nach X-Plane kopieren + ad-hoc codesignen
make lint      # clang-tidy (optional, braucht brew llvm)
```

## Offene Fragen / nächste Schritte
1. ~~Struktur ATC Transmission JSON → in `xp_wellys_vfr_atc` nachschauen~~ → **erledigt** (`docs/atc_transmission_json.md`, Fixture in `testdata/`)
2. ~~Struktur Flugdaten JSON → in `xp_pilot` nachschauen~~ → **erledigt** (`docs/flight_log_json.md`, Fixture in `testdata/`)
3. ~~LLM-Client-Abstraktion → analysieren~~ → **erledigt** (übernommen, auf LM reduziert)
4. ~~Caching-Strategie Airport-Scores (SQLite vs. JSON)~~ → **entschieden: JSON**
   (`<plugin>/data/airport_scores.json`, nlohmann schon vendored; Invalidierung
   über `prompt_version` + `input_sig`). Details: `docs/airport_score_cache.md`.
5. **Airport-Schwierigkeits-Prompt** → noch auszuarbeiten

### Roadmap
- [x] Projekt-Grundgerüst
- [~] Airport-Daten-Layer: `apt.dat`-Parser + DACH-Filter erledigt (SDK-frei,
      `src/airports/`, Tests in `tests/test_airports.cpp` + Fixture
      `testdata/sample_apt.dat`); Runtime-Laden + Caching offen → #4
- [ ] Airport-Schwierigkeits-Prompt + Scoring
- [ ] Post-Flight-Bewertung: Zeitkorrelation ATC-/Flugdaten-JSON + LLM-Urteil
      (JSON-Formate dokumentiert: `docs/`; Zeitkorrelations-Strategie + Producer-Fix:
      `docs/post_flight_correlation.md`)
- [ ] Gamification-Layer

## Konventionen (etabliert)
- Module-Namespace pro Verzeichnis (`logging`, `backends`, `settings`,
  `trainer_ui`); Interfaces mit `I`-Prefix, konkrete Impls mit Provider-Suffix.
- SDK-freier Code (engine-tauglich) → `xp_trainer_engine` OBJECT-Lib; alles mit
  XPLM-Abhängigkeit → Plugin-Modul.
- GPL-3.0-Header in jeder Quelldatei; Log-Tag `[xp_wellys_vfr_trainer]`.
- Async-Regel: kein Worker-Thread überlebt `XPluginDisable`/`XPluginStop`;
  Netzwerk nie auf dem Hauptthread.
