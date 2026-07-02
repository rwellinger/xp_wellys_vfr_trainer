# xp_wellys_vfr_trainer

VFR-Trainer mit Gamification-Layer für **X-Plane 12** (macOS Apple Silicon &
Intel, **Windows x64**). Das Plugin motiviert zu echten VFR-Flügen und
ATC-Training im DACH-Raum, indem es Flüge bewertet und Flugplätze nach
Schwierigkeit einordnet.

> **Status: Weitgehend fertig.** Airport-Daten-Layer (`apt.dat`-Parser + DACH-Filter),
> LLM-Schwierigkeits-Scoring mit JSON-Cache, Pre-Flight-Flugvorschläge inkl.
> FMS-Übernahme und die Post-Flight-Bewertung (Zeitkorrelation der ATC-/Flugdaten-JSONs
> + LLM-Urteil) sind umgesetzt; Windows wird via CI voll unterstützt. Offen sind nur
> noch der Gamification-Layer und die Post-Flight-History — siehe [Roadmap](#roadmap).

## Idee

- **Airport-Schwierigkeits-Score** – jeder Flugplatz bekommt einmalig (gecacht)
  einen Schwierigkeits-Wert via LLM.
- **Post-Flight-Bewertung** – nach jeder Session wird der Flug bewertet. Dazu
  werden die ATC-Transmissions (`xp_wellys_devfr_atc`) und die Flugdaten
  (`xp_pilot`) zeitkorreliert und vom LLM beurteilt.
- **Kein Live-LLM** für Airport-Suche oder -Filterung – nur die beiden Fälle oben.

Datenquelle für Flugplätze ist die X-Plane-`apt.dat` (keine externe DB),
gefiltert auf den DACH-Raum (ICAO-Prefixes `ED`, `ET`, `LS`, `LO`).

## Voraussetzungen

- macOS (Apple Silicon oder Intel), X-Plane **12.1+** (die FMS-Übernahme nutzt die XPLM410-Multi-FPL-API)
- `cmake` ≥ 3.26, ein C++17-Compiler (Apple Clang)
- `curl`, `unzip` (für `make setup`)
- optional: `clang-format` / `clang-tidy` aus `brew install llvm` für `make format` / `make lint`

## Build & Install

```bash
make setup     # lädt X-Plane SDK, Dear ImGui, nlohmann/json, Catch2 (nach sdk/ + vendor/)
make build     # baut das universal .xpl (arm64 + x86_64) -> build/xp_wellys_vfr_trainer.xpl
make test      # baut + führt die Catch2-Unit-Tests aus
make install   # kopiert + ad-hoc-codesigned das Plugin nach X-Plane
```

Der Install-Pfad ist:

```
<X-Plane 12>/Resources/available plugins/xp_wellys_vfr_trainer/
├── mac_x64/xp_wellys_vfr_trainer.xpl
├── win_x64/xp_wellys_vfr_trainer.xpl   (aus einem Release / Windows-Artifact)
└── data/settings.json
```

`make install` überschreibt eine vorhandene `settings.json` **nicht**. Nach der
Installation in X-Plane unter *Plugins → Welly's VFR Trainer* das Fenster öffnen.

Weitere Targets: `make lint`, `make format`, `make clean`, `make distclean`
(siehe `make help`).

## Windows

Windows wird **cloud-only** voll unterstützt (OpenAI / Mistral über libcurl) —
funktional identisch zum macOS-x86_64-Slice. Kompiliert wird ausschließlich über
**GitHub Actions** (`windows-latest`, MSVC + Ninja, statisches libcurl aus vcpkg
mit Schannel-TLS); ein lokaler Windows-Toolchain ist nicht nötig. Das erzeugte
`win_x64/xp_wellys_vfr_trainer.xpl` ist ein self-contained DLL-Drop-in ohne
zusätzliche DLLs.

```bash
make ci-remote      # stößt den GitHub-Actions-Build an (mac + Windows)
make win-artifact   # lädt das neueste Windows-.xpl nach dist-win/
```

Zum Testen ohne Release: `dist-win/win_x64/xp_wellys_vfr_trainer.xpl` (plus
`data/settings.json`) nach
`<X-Plane 12>/Resources/plugins/xp_wellys_vfr_trainer/win_x64/` kopieren.

Der **API-Key** liegt unter Windows im **Windows Credential Manager** (DPAPI-
verschlüsselt), analog zum macOS-Keychain — nie im Klartext auf der Platte.

Ein Release (`v*`-Tag) faltet den macOS-universal- und den Windows-Slice zu einem
Drop-in-ZIP (`mac_x64/` + `win_x64/`) und schreibt die SkunkCrafts-Updater-
Kontrolldateien.

## LLM-Konfiguration

Das Plugin nutzt einen Cloud-LLM-Provider (Nutzerwahl) – kein lokales Modell.

- **Provider** wird über `backend_mode` in `data/settings.json` gewählt:
  `"openai"` (Default) oder `"mistral"`.
- **API-Keys** werden im **macOS-Keychain** (bzw. **Windows Credential Manager**)
  gespeichert (nie in `settings.json`), in separaten Einträgen pro Provider
  (`com.xp_wellys_vfr_trainer.openai` / `.mistral`), sodass beide parallel
  bestehen können.
- Modellnamen: `openai_lm_model` (Default `gpt-4o-mini`),
  `mistral_lm_model` (Default `mistral-large-latest`).

Solange kein Key hinterlegt ist, bleibt das Sprachmodell „not loaded" und das
Fenster weist darauf hin.

## Architektur

Zwei Build-Artefakte aus einer Quelle:

- **`xp_trainer_engine`** (CMake OBJECT-Library) – X-Plane-SDK-freier Kern
  (`logging`, async LM-Dispatcher `manager`, provider-agnostische Helfer). Wird
  von den Catch2-Tests direkt wiederverwendet.
- **`xp_wellys_vfr_trainer.xpl`** (Plugin-Modul) – Engine + X-Plane-spezifischer
  Code (`main`, ImGui-UI, Settings, Keychain, Cloud-Clients, Loader).

Die LLM-Anbindung ist provider-agnostisch hinter dem Interface
`backends::ILanguageModel` (`OpenAiLm` / `MistralLm`, libcurl + nlohmann/json).
Netzwerkaufrufe laufen auf abgesetzten Worker-Threads (QoS `UTILITY`); die
Ergebnisse werden im Flight-Loop über `drain_callback_queue()` auf den
X-Plane-Hauptthread zurückgereicht, damit die Sim nie blockiert.

> **Hinweis zum Build:** Im Gegensatz zum Referenz-Plugin
> `../xp_wellys_vfr_atc` (das wegen arm64-only Metal/onnxruntime-Backends zwei
> Build-Stages + `lipo` braucht) ist der Trainer reines Cloud-Plugin und damit
> architektur-neutral. Ein einziger CMake-Build mit
> `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` erzeugt das universal `.xpl` direkt.

### Projektstruktur

```
src/
├── main.cpp                  # Plugin-Lifecycle, Flight-Loop, Menü, Command
├── core/logging.*            # Pluggable Sink -> XPLMDebugString
├── backends/
│   ├── i_language_model.hpp  # ILanguageModel-Interface
│   ├── manager.*             # async LM-Dispatch + Callback-Queue
│   ├── openai_lm.*           # OpenAI /v1/chat/completions
│   ├── mistral_lm.*          # Mistral /v1/chat/completions
│   ├── openai_common.*       # provider-agnostische Helfer
│   └── loader.*              # Provider-Auswahl + LM-Registrierung
├── airports/
│   ├── apt_dat_parser.*      # apt.dat-Parser + DACH-Filter
│   ├── airport_db.*          # In-Memory-DB (Background-Load)
│   ├── airport_scorer.*      # LLM-Schwierigkeits-Scoring (Batch)
│   ├── airport_score_cache.* # JSON-Cache (prompt_version + input_sig)
│   └── geo.*                 # Großkreis-Distanz
├── preflight/
│   └── flight_suggester.*    # regelbasierte Flugvorschläge + Ground-Filter
├── postflight/
│   ├── discovery.*           # findet neueste ATC-/Flugdaten-JSONs
│   ├── atc_log.* flight_log.* # JSON-Parser
│   ├── correlation.*         # Zeitkorrelation ts <-> track[].t
│   ├── evaluator.*           # LLM-Urteil (Sub-Scores + Summary)
│   └── report_cache.*        # persistente Trainingsreports
├── persistence/
│   ├── settings.*            # settings.json I/O
│   └── keychain.*            # API-Keys (macOS-Keychain / Win Credential Manager)
└── ui/
    ├── trainer_ui.*          # ImGui-Fenster (Pre-/Post-Flight-Tabs)
    └── clipboard.*           # Zwischenablage (mm = macOS, _win = Windows)
tests/                        # Catch2-Unit-Tests gegen xp_trainer_engine
data/settings.json            # Default-Einstellungen
```

## Roadmap

- [x] Projekt-Grundgerüst (Build, Plugin-Skeleton, LLM-Abstraktion, UI-Platzhalter)
- [x] Airport-Daten-Layer: `apt.dat`-Parser, DACH-Filter, JSON-Cache
- [x] Airport-Schwierigkeits-Prompt + LLM-Scoring (regelbasierter Fallback ohne Key/Cache)
- [x] Pre-Flight-Flugvorschläge inkl. Ground-Control-Filter + FMS-Übernahme
- [x] Post-Flight-Bewertung: Zeitkorrelation der ATC-/Flugdaten-JSONs + LLM-Urteil
- [x] Windows-Portierung + CI (GitHub Actions, SkunkCrafts-Updater)
- [ ] Post-Flight-History: bereits bewertete Sessions durchblättern (#13)
- [ ] Gamification-Layer (#7)

### Nicht-Ziele

Kein Exam-Zwang, keine Flugzeug-Einschränkung, kein Live-LLM für Airport-Suche,
keine externe Airport-Datenbank.

## Lizenz

GNU GPL-3.0-or-later – siehe [LICENSE](LICENSE).
