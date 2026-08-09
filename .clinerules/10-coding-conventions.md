# Coding-Konventionen (C++17)

- C++17; Exceptions nur an der Plugin-Boundary abfangen (`main.cpp`) — nie
  über die X-Plane-Grenze hinweg
- Keine Exceptions in Destruktoren
- X-Plane-API **nur** auf dem Main-Thread; Netzwerk/LLM-Calls auf
  `std::thread`-Workern + atomarem Status, nie blockierend im Flight-Loop
- Logging: `XPLMDebugString` → Log.txt, Tag `[xp_wellys_vfr_trainer]`,
  **nur ASCII** (0x20–0x7E) — UTF-8 wird im Log/ImGui zu `?`
- JSON: `nlohmann::json` (`vendor/json.hpp`)
- Self-contained Module-Header, keine zirkulären Includes
- Includes mit Subdir-Präfix: `#include "backends/openai_lm.hpp"`
- `make format` / `make lint` / `make build` bevorzugen statt Ad-hoc-Toolchain
- Flache Verschachtelung: early returns/Helper statt tiefe `if`/`switch`
- Clean Code: lesbare Namen, kleine Funktionen, keine spekulativen
  Abstraktionen
- **SDK-frei bleibt SDK-frei**: `xp_trainer_engine` (OBJECT-Lib) darf kein
  `<XPLM*.h>` einbinden — jede TU mit XPLM-Abhängigkeit gehört ins
  Plugin-Modul (`xp_wellys_vfr_trainer`-Target in `CMakeLists.txt`)
- Namespace pro Verzeichnis (`logging`, `backends`, `settings`, `deps`,
  `trainer_ui`, …); Interfaces mit `I`-Prefix (`ILanguageModel`,
  `IPluginProbe`), konkrete Implementierungen mit Provider-Suffix
  (`OpenAiLm`, `MistralLm`)
- GPL-3.0-Header in jeder Quelldatei
- Async-Regel: kein Worker-Thread überlebt `XPluginDisable`/`XPluginStop`
- Keine API-Keys in `settings.json` — nur Keychain (macOS) /
  Credential Manager (Windows), Logs nur mit den letzten 4 Zeichen
