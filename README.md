# xp_wellys_vfr_trainer

VFR-Trainer mit Gamification-Layer für **X-Plane 12** (macOS, Apple Silicon &
Intel). Das Plugin motiviert zu echten VFR-Flügen und ATC-Training im
DACH-Raum, indem es Flüge bewertet und Flugplätze nach Schwierigkeit einordnet.

> **Status: Grundgerüst.** Das Plugin lädt in X-Plane, zeigt ein Menü und ein
> (noch leeres) Fenster und bringt die LLM-Anbindung (OpenAI / Mistral) mit.
> Der Gamification-Layer, das Airport-Scoring und die Post-Flight-Bewertung
> sind noch nicht implementiert — siehe [Roadmap](#roadmap).

## Idee

- **Airport-Schwierigkeits-Score** – jeder Flugplatz bekommt einmalig (gecacht)
  einen Schwierigkeits-Wert via LLM.
- **Post-Flight-Bewertung** – nach jeder Session wird der Flug bewertet. Dazu
  werden die ATC-Transmissions (`xp_wellys_devfr_atc`) und die Flugdaten
  (`xp_pilot`) zeitkorreliert und vom LLM beurteilt.
- **Kein Live-LLM** für Airport-Suche oder -Filterung – nur die beiden Fälle oben.

Datenquelle für Flugplätze ist die X-Plane-`apt.dat` (keine externe DB),
gefiltert auf den DACH-Raum (ICAO-Prefixes `ED`, `LS`, `LO`, `ET`, `LZ`).

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
└── data/settings.json
```

`make install` überschreibt eine vorhandene `settings.json` **nicht**. Nach der
Installation in X-Plane unter *Plugins → Welly's VFR Trainer* das Fenster öffnen.

Weitere Targets: `make lint`, `make format`, `make clean`, `make distclean`
(siehe `make help`).

## LLM-Konfiguration

Das Plugin nutzt einen Cloud-LLM-Provider (Nutzerwahl) – kein lokales Modell.

- **Provider** wird über `backend_mode` in `data/settings.json` gewählt:
  `"openai"` (Default) oder `"mistral"`.
- **API-Keys** werden im **macOS-Keychain** gespeichert (nie in `settings.json`),
  in separaten Einträgen pro Provider
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
├── persistence/
│   ├── settings.*            # settings.json I/O
│   └── keychain.*            # API-Keys im macOS-Keychain
└── ui/trainer_ui.*           # ImGui-Fenster (Platzhalter)
tests/                        # Catch2-Unit-Tests gegen xp_trainer_engine
data/settings.json            # Default-Einstellungen
```

## Roadmap

- [x] Projekt-Grundgerüst (Build, Plugin-Skeleton, LLM-Abstraktion, UI-Platzhalter)
- [ ] Airport-Daten-Layer: `apt.dat`-Parser, DACH-Filter, Caching (SQLite vs. JSON)
- [ ] Airport-Schwierigkeits-Prompt + Scoring
- [ ] Post-Flight-Bewertung: Zeitkorrelation der ATC-/Flugdaten-JSONs + LLM-Urteil
- [ ] Gamification-Layer

### Nicht-Ziele

Kein Exam-Zwang, keine Flugzeug-Einschränkung, kein Live-LLM für Airport-Suche,
keine externe Airport-Datenbank.

## Lizenz

GNU GPL-3.0-or-later – siehe [LICENSE](LICENSE).
