# Build & Test

Immer über `make`, nicht roh mit `cmake`/`ctest` arbeiten (siehe `Makefile`):

```bash
make setup       # SDK (sdk/) + vendor (ImGui, nlohmann/json, Catch2) — nicht versioniert
make build       # universal .xpl (arm64+x86_64) -> build/xp_wellys_vfr_trainer.xpl
make test        # Catch2 --order rand --rng-seed 42, gegen build-test/ (Host-Arch)
make install     # nach X-Plane kopieren + ad-hoc codesignen (überschreibt settings.json NICHT)
make lint        # clang-tidy (braucht brew llvm; *_win.cpp ausgeschlossen)
make format      # clang-format auf src/*.cpp/*.hpp
make ci-remote   # GitHub-Actions-Build anstoßen (mac + Windows)
```

## Zwei Build-Bäume, eine Quelle
- `xp_trainer_engine` (CMake OBJECT-Lib) — SDK-frei, wird von den
  Catch2-Tests direkt genutzt (`build-test/`)
- `xp_wellys_vfr_trainer` (MODULE) — Engine + XPLM-spezifischer Code
  (`main.cpp`, `ui/`, `persistence/`, `fms/`) → `build/`

## Windows
- Kompiliert **ausschließlich** über GitHub Actions (`make ci-remote` /
  `make win-artifact`) — kein lokaler MSVC-Toolchain vorhanden/nötig
- `*_win.cpp`-Dateien nie lokal auf macOS linten/bauen (ziehen `<windows.h>`)

## Nach Codeänderungen
Vor Abschluss einer Aufgabe mindestens `make test` grün bekommen; bei
Änderungen an `src/main.cpp`, `ui/`, oder plattformspezifischem Code
zusätzlich `make build` verifizieren.
