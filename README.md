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

Die Phasen 1 und 2 von 8 sind umgesetzt: Build-System, Hauptfenster mit dunklem Theme, portable
Pfadverwaltung, Einstellungen und Logging, dazu der GitHub-Client und die Projekt-Registry mit
Zwischenspeicher, Offline-Verhalten und Bibliotheksseite. Releases, Downloads, Installation und
Updates folgen. Details stehen in [docs/02-implementierungsplan.md](docs/02-implementierungsplan.md).

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
| `SettingsTests` | Sofortiges Speichern, beschädigte Dateien, Migration |
| `LoggerTests` | Format, Rotation, Größenlimit, Pfad-Schwärzung |
| `RegistryTests` | Registry-Parser: alle Prüfregeln für Einträge, Pfade und Namen |
| `GitHubClientTests` | URL-Policy, Wiederholungen, Rate Limit, Größenlimit, Redirects |
| `RegistryManagerTests` | Laden, Cache, Offline-Verhalten, beschädigter Cache |
| `UiSmokeTests` | Navigation, Einstellungen, Bibliothek mit Filtern und Fehlerzuständen |

Der UI-Test läuft ohne sichtbares Fenster und legt Screenshots aller Seiten in
`build\release\screenshots` ab.

## Aufbau

```
cmake/Branding.cmake   Produktname, EXE-Namen, GitHub-Koordinaten, Registry-URL
src/app/               Startsequenz und Branding
src/core/              PathManager, SettingsManager, Logger, JSON-Dateien
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
downloads/    geprüfte Downloads
temp/         Teil-Downloads und Staging
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
