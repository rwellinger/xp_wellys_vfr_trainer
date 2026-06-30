# CONCEPT.md – Konzept

## Ziel
Gamification-Layer über `xp_wellys_devfr_atc`, der VFR/ATC-Training im
DACH-Raum motiviert – ohne Zwang, ohne Prüfungen. Das Plugin soll den
Nutzer dazu bringen, echte Flüge zu machen und dabei das ATC-Plugin
zu verwenden.

## Basis / Template
Fork von `../xp_wellys_vfr_atc`. Folgende Komponenten werden direkt
übernommen:

- UI-Framework
- LLM-Client-Abstraktion (Mistral / OpenAI)
- Makefile / Build-System
- 3rd-Party-Libraries
- Unit-Test-Struktur
- X-Plane Plugin Boilerplate

Claude Code soll `../xp_wellys_vfr_atc` als Referenz verwenden und
die relevanten Strukturen, Patterns und Abstraktionen adaptieren –
nicht blind kopieren.

## Plugin-Name
`xp_wellys_vfr_trainer` – "Welly's VFR Trainer"

## Architektur

### 1. Startup (einmalig)
- `apt.dat` einlesen und auf DACH filtern:
  - ICAO-Prefixes `ED`, `LS`, `LO`, `ET`, `LZ`
  - Filter-Kriterien (regelbasiert, kein LLM):
    - Befestigte oder Graspiste mit Mindestlänge (GA-tauglich)
    - ICAO-Code vorhanden
    - Tower- oder AFIS-Frequenz vorhanden
  - Ergebnis: lokale DACH Airport DB (SQLite oder JSON)
- LLM bewertet jeden Airport einmalig → Schwierigkeits-Score
  wird gecacht (nicht bei jeder Session neu abgefragt)
  - Schwierigkeits-Kriterien für LLM-Prompt:
    - Controlled vs. uncontrolled
    - Platzrunden-Komplexität
    - Umgebung (Berge, CTR, TMA in der Nähe)
    - Typischer Traffic

### 2. Pre-Flight Dialog
Regelbasiert – kein LLM. **Departure = aktuelle Flugzeugposition**: der Trainer
löst den nächstgelegenen GA-tauglichen Platz auf (in der Praxis der Platz, an dem
du stehst) und schlägt Ziele im Umkreis vor. Nutzer wählt:

- Ziel-Facility: Beliebig / Tower / AFIS
- Maximale Distanz (km) oder Flugdauer (min)
- Schwierigkeitsgrad: Einfach / Mittel / Anspruchsvoll
- Flugzeug: freie Wahl, kein Zwang, kein Exam

Kein Land-Filter (redundant, da Departure = Position).

Ausgabe: erreichbare Ziele ab dem aktuellen Platz (Departure-ICAO → Ziel-ICAO),
nach Distanz sortiert (nächste zuerst).

### 3. Aktiver Flug (X-Plane)
Zwei Datenquellen laufen parallel:

- `xp_wellys_devfr_atc` → ATC Transmission JSON
  (alle ATC-Sprüche mit Zeitstempel)
- `xp_pilot` → Flugdaten JSON
  (Koordinaten, Altitude, Speed mit Zeitstempel)

### 4. Post-Flight Bewertung
- Beide JSONs werden zeitkorreliert
- Korreliertes Ergebnis → LLM (Provider-Wahl des Nutzers)
- System-Prompt: DACH VFR/ATC Regelwerk-Kontext
- Bewertungs-Kriterien:
  - Phraseologie korrekt und vollständig?
  - Readback korrekt?
  - Reaktionszeit auf ATC-Anweisungen
  - Position beim Funken (war Pilot wirklich wo er sagte?)
  - Fehlende Calls (Downwind, Final, …)
  - Pünktlichkeit / Ablaufkontrolle
- Ausgabe: strukturierter Report

## LLM-Strategie

| Aufgabe                  | Methode                  |
| ------------------------ | ------------------------ |
| Airport-Liste filtern    | regelbasiert (`apt.dat`) |
| Schwierigkeits-Score     | LLM einmalig, gecacht    |
| Flugvorschlag generieren | regelbasiert             |
| Post-Flight Bewertung    | LLM, pro Session         |

Provider: Mistral / OpenAI – Wahl durch Nutzer, bestehende
Abstraktion aus `../xp_wellys_vfr_atc` wiederverwenden.

## Offene Punkte für Claude Code
- JSON-Struktur von `xp_wellys_devfr_atc` (ATC Transmission)
  analysieren → Felder für Zeitkorrelation identifizieren
- JSON-Struktur von `xp_pilot` analysieren
  → Koordinaten- und Zeitstempel-Format prüfen
- LLM-Client-Abstraktion in `../xp_wellys_vfr_atc` prüfen
  → direkt wiederverwenden oder minimal anpassen
- Airport-Schwierigkeits-Prompt ausarbeiten
  → System-Prompt + Beispiel-Output definieren
- Caching-Strategie für Airport-Scores festlegen
  → SQLite lokal oder JSON-File?

## Nicht-Ziele
- Kein Exam-Zwang
- Keine Flugzeug-Einschränkung
- Kein Live-LLM für Airport-Suche
- Keine externe Airport-Datenbank (nur `apt.dat`)
