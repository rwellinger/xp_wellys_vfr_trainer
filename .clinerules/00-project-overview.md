# xp_wellys_vfr_trainer — Projektüberblick (immer aktiv)

C++17 X-Plane 12 Plugin (macOS Universal `arm64+x86_64` + Windows `win_x64`,
cloud-only, kein IFR). Gamification-/VFR-Trainer-Aufsatz über das
Companion-Plugin `xp_wellys_vfr_atc`. Volle Details: `CLAUDE.md` im
Projekt-Root — bei Widerspruch gilt `CLAUDE.md`.

## Sprache
- Code + Kommentare: **Englisch**
- Kommunikation mit dem Entwickler: **Deutsch**

## Template-Projekt
`../xp_wellys_vfr_atc` ist die Referenz. Vor neuer Implementierung dort
nachschauen (UI-Patterns, LLM-Client-Abstraktion, Makefile-Struktur,
Test-Patterns, X-Plane-SDK-Verwendung) — **adaptieren, nicht blind kopieren**,
Abweichungen begründen (siehe "Architektur-Entscheidungen" in `CLAUDE.md`).

## Nicht-Ziele (nicht implementieren)
- Kein Exam-Zwang, keine Flugzeug-Einschränkung
- Kein Live-LLM für Airport-Suche/-Filterung (nur Difficulty-Score + Post-Flight)
- Keine externe Airport-DB (nur `apt.dat`)

## Feature-Split
- **Standalone**: Airport-Suche + FMS-Übernahme
- **Braucht beide Plugins** (`xp_wellys_devfr_atc` + `xp_pilot`, installiert
  UND aktiviert): Post-Flight-Reporting, Session-Scoring — gated über
  `deps::all_ready()` (`src/dependencies/`)

## Coding-Regeln
- Kurz und präzise, kein Over-Engineering
- Keine Annahmen bei Unklarheiten — nachfragen
- Faktenbasiert, keine Halluzinationen bei ICAO-Codes/API-Details
- DACH-Filter: ICAO-Prefixes `ED`, `ET` (DE), `LS` (CH), `LO` (AT). **Kein** `LZ`
  (Slowakei) — Vorsicht: `CONCEPT.md` (älter) listet `LZ` noch fälschlich mit,
  `CLAUDE.md` ist hier die aktuellere Quelle.
