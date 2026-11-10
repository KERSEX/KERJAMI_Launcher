<p align="center">
  <img src="resources/images/logo-source.png" alt="KERJAMI Launcher" width="280">
</p>

# KERJAMI Launcher

Portabler Launcher für freigegebene Community-Projekte: entdecken, installieren, starten,
aktualisieren. Native Windows-Anwendung in C++20 mit Qt 6 Widgets, ohne Installation und ohne
Administratorrechte.

Gemeinsames Projekt von Hakenzahn (Hejki), Amil und KERS. Repository:
[KERSEX/KERJAMI_Launcher](https://github.com/KERSEX/KERJAMI_Launcher). Der Produktname ist
vorläufig und steht nur in [cmake/Branding.cmake](cmake/Branding.cmake).

## Stand

Die Phasen 1 bis 5 von 8 sind umgesetzt: Build-System, Hauptfenster mit dunklem Theme, portable
Pfadverwaltung, Einstellungen und Logging, der GitHub-Client, die Projekt-Registry mit
Zwischenspeicher und Offline-Verhalten, die Auswahl der neuesten stabilen Version, die
Download-Warteschlange mit Fortschritt und die Installation: SHA-256-Prüfung, ZIP-Prüfung und
sicheres Entpacken, Staging mit Backup und Rollback, Deinstallation und Start. Projekte werden in
der Bibliothek installiert, gestartet und entfernt. Der Launcher erkennt laufende Projekte und
prüft installierte Projekte auf neue Versionen: je Projekt manuell (Hinweis und Rückfrage) oder
automatisch (wird installiert, sobald das Projekt geschlossen ist). Das Selbstupdate des Launchers
folgt. Details stehen in
[docs/02-implementierungsplan.md](docs/02-implementierungsplan.md).

## Bauen

Voraussetzungen: Qt 6.5 oder neuer, CMake 3.24 oder neuer, Ninja, ein C++20-Compiler.
Entwickelt wird mit statischem Qt 6.11.2 und llvm-mingw (clang).

1. `scripts/build.local.example.psd1` nach `scripts/build.local.psd1` kopieren und die Pfade
   anpassen. Die Datei wird von Git ignoriert.
2. Bauen und testen:

```powershell
.\scripts\build.ps1 -Test
```

Das Ergebnis liegt in `build\release\bin\KERJAMI.exe`. Dieser Ordner ist während der
Entwicklung zugleich der portable Programmordner: Einstellungen, Logs und Bibliothek entstehen
neben der EXE.

Weitere Optionen: `-Config Debug`, `-Clean`, `-QtDir <Pfad>`, `-ToolchainDir <Pfad>`.

## Tests

```powershell
.\scripts\build.ps1 -Test
```

| Test | Prüft |
|---|---|
| `PortableTests` | Pfade, Verschieben des Ordners, fehlende Schreibrechte |
| `SettingsTests` | Sofortiges Speichern, beschädigte Dateien, Migration, Einstellungen pro Projekt |
| `LoggerTests` | Format, Rotation, Größenlimit, Pfad-Schwärzung |
| `RegistryTests` | Registry-Parser: alle Prüfregeln für Einträge, Pfade und Namen |
| `GitHubClientTests` | URL-Policy, Wiederholungen, Rate Limit, Größenlimit, Redirects |
| `RegistryManagerTests` | Laden, Cache, Offline-Verhalten, beschädigter Cache |
| `SemVerTests` | Versionen lesen und vergleichen nach SemVer 2.0 |
| `ReleaseTests` | Release-Antwort lesen, Datei wählen, Prüfsumme und Prüfsummendatei, ReleaseManager |
| `DownloadTests` | Download-Warteschlange, Fortschritt, Abbruch, Größen- und Speicherfehler |
| `SecurityTests` | SHA-256, ZIP-Leser mit kaputten und feindlichen Archiven, Pfadregeln, Validator, Extraktor |
| `InstallerTests` | Installation, Update mit Backup, Rollback, Deinstallation, gesperrte Dateien, Links |
| `InstallServiceTests` | Die ganze Kette gegen einen lokalen Server, falsche Hashes, Abbruch, Vorbereiten |
| `ProcessTests` | Start, Mehrfachstart, Prozessende, von außen gestartete Projekte, Nachkommen |
| `UpdateTests` | Update-Prüfung, manuell und automatisch, laufende Projekte, Fehler, Warteschlange |
| `UiSmokeTests` | Navigation, Einstellungen, Bibliothek mit Projektkarten und Update-Anzeige, Downloads-Seite |

Der UI-Test läuft ohne sichtbares Fenster und legt Screenshots aller Seiten in
`build\release\screenshots` ab.

## Aufbau

```
cmake/Branding.cmake   Produktname, EXE-Namen, GitHub-Koordinaten, Registry-URL
src/app/               Startsequenz und Branding
src/core/              PathManager, SettingsManager, Logger, JSON-Dateien
src/github/            GitHub-Client, Registry, Releases
src/downloads/         Download-Warteschlange
src/security/          SHA-256, ZIP-Leser, Archiv-Prüfung
src/installer/         Installer, Deinstallation, Liste der Installationen, Start
src/process/           Prozessüberwachung
src/updater/           Update-Prüfung der Projekte
src/models/            Datentypen
src/ui/                Hauptfenster, Seiten, Controls, Theme
resources/images/      Logo und Icons (erzeugt aus logo-source.png)
resources/styles/      Style Sheet
tests/                 Qt Test, über CTest
scripts/               Build-Skript, Icon-Erzeugung
docs/                  Bestandsaufnahme, Plan, Entscheidungen
```

## Portabler Programmordner

```
KERJAMI.exe
launcher/     Einstellungen und Launcher-Status
library/      installierte Projekte
downloads/    heruntergeladene Archive bis zur Installation
temp/         Teil-Downloads, Staging, entfernte Projekte vor dem Löschen
cache/        Registry- und Release-Cache
logs/         Logdateien
```

Der Ordner kann als Ganzes verschoben werden. Gespeichert werden nur relative Pfade.

## Logo ändern

`resources/images/logo-source.png` ersetzen und danach ausführen:

```powershell
.\scripts\make-icons.ps1
```

Das Skript erzeugt Logo, Emblem, Fenster-Icons und die `app.ico` der EXE neu. Die Ergebnisse
werden eingecheckt. Details in [docs/03-entscheidungen.md](docs/03-entscheidungen.md) (D-15).

## Dokumentation

- [Technische Bestandsaufnahme](docs/01-bestandsaufnahme.md)
- [Implementierungsplan](docs/02-implementierungsplan.md)
- [Entscheidungen](docs/03-entscheidungen.md)
- [Vorlage für das Registry-Repository](docs/registry/README.md)
