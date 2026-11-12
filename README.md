<p align="center">
  <img src="resources/images/Kerjami-Launcher-Logo.png" alt="KERJAMI Launcher" width="280">
</p>

# KERJAMI Launcher

Portabler Launcher für freigegebene Community-Projekte: entdecken, installieren, starten,
aktualisieren. Native Windows-Anwendung in C++20 mit Qt 6 Widgets, ohne Installation und ohne
Administratorrechte.

Gemeinsames Projekt von Hakenzahn (Hejki), Amil und KERS. Repository:
[KERSEX/KERJAMI_Launcher](https://github.com/KERSEX/KERJAMI_Launcher). Der Produktname ist
vorläufig und steht nur in [cmake/Branding.cmake](cmake/Branding.cmake).

## Stand

Alle acht Phasen sind umgesetzt: Build-System, Hauptfenster mit dunklem Theme, portable
Pfadverwaltung, Einstellungen und Logging, der GitHub-Client, die in das Programm eingebaute
Projektliste, die Auswahl der neuesten stabilen Version, die
Download-Warteschlange mit Fortschritt und die Installation: SHA-256-Prüfung, ZIP-Prüfung und
sicheres Entpacken, Staging mit Backup und Rollback, Deinstallation und Start. Projekte werden im
Store gefunden und installiert, in der Bibliothek gestartet und aktualisiert und auf ihrer
Detailseite entfernt. Der Launcher erkennt laufende Projekte und
prüft installierte Projekte auf neue Versionen: je Projekt manuell (Hinweis und Rückfrage) oder
automatisch (wird installiert, sobald das Projekt geschlossen ist). Der Launcher aktualisiert sich
selbst: Er lädt das neue Release, prüft es gegen Prüfsumme und Manifest und übergibt an eine Kopie
seiner selbst (`KERJAMI_Launcher.exe --apply-update`), die die Programmdateien nach dem Beenden in einer
Transaktion mit Backup und Rollback austauscht und den Launcher neu startet. Das Programm ist eine
einzige Datei. Projekte dürfen als ZIP oder als einzelne EXE veröffentlicht werden. Die Oberfläche hat ein
Dashboard (Updates, installierte und zuletzt gestartete Projekte, Empfehlungen), eine Detailseite je Projekt
(Version, Changelog, Download-Größe, GitHub-Link, alle Aktionen) und eine Speicherverwaltung. Ein Skript baut das
portable Paket und prüft es von außen. Details stehen in
[docs/02-implementierungsplan.md](docs/02-implementierungsplan.md).

Noch nicht geklärt oder nie gelaufen: die Qt-Lizenz beim statischen Linken (E-01), die GitHub-Workflows, ein Build
mit MSVC und geteiltem Qt, eine Signatur der EXE. Die Projektliste enthält KERS Overlay und Tankstellenmeal; ein ZIP-Release (Tankstellenmeal, 414 MB) und eine einzelne EXE (KERS Overlay) sind echt installiert und gestartet worden, ein Update zwischen zwei echten Versionen noch nicht (E-02).

## Bauen

Voraussetzungen: Qt 6.5 oder neuer, CMake 3.24 oder neuer, Ninja, ein C++20-Compiler.
Entwickelt wird mit statischem Qt 6.11.2 und llvm-mingw (clang).

1. `scripts/build.local.example.psd1` nach `scripts/build.local.psd1` kopieren und die Pfade
   anpassen. Die Datei wird von Git ignoriert.
2. Bauen und testen:

```powershell
.\scripts\build.ps1 -Test
```

Das Ergebnis liegt in `build\release\bin\KERJAMI_Launcher.exe`. Dieser Ordner ist während der
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
| `RegistryManagerTests` | Die eingebaute Projektliste ist im Programm und gültig, Laden, unbrauchbare Dokumente |
| `SemVerTests` | Versionen lesen und vergleichen nach SemVer 2.0 |
| `ReleaseTests` | Release-Antwort lesen, Datei wählen, Prüfsumme und Prüfsummendatei, ReleaseManager |
| `DownloadTests` | Download-Warteschlange, Fortschritt, Abbruch, Größen- und Speicherfehler |
| `SecurityTests` | SHA-256, ZIP-Leser mit kaputten und feindlichen Archiven, Pfadregeln, Validator, Extraktor |
| `InstallerTests` | Installation, Update mit Backup, Rollback, Deinstallation, gesperrte Dateien, Links |
| `InstallServiceTests` | Die ganze Kette gegen einen lokalen Server, falsche Hashes, Abbruch, Vorbereiten |
| `ProcessTests` | Start, Mehrfachstart, Prozessende, von außen gestartete Projekte, Nachkommen |
| `UpdateTests` | Update-Prüfung, manuell und automatisch, laufende Projekte, Fehler, Warteschlange |
| `UpdateTransactionTests` | Manifest-Parser, Austausch der Programmdateien mit Backup, Rollback, Absturz mittendrin und Wiederherstellung |
| `UpdaterTests` | Die echte `KERJAMI_Launcher.exe` im Austausch-Modus: Austausch, Warten auf den Launcher, Ablehnung, Bericht, Neustart |
| `LauncherUpdateTests` | Launcher-Update: Prüfung, feindliche Pakete, Abbruch, Übergabe, Selbstupdate von Anfang bis Ende |
| `StorageTests` | Größen und Aufräumen von Downloads, temporären Dateien und Sicherungen, nie durch einen Link |
| `UiSmokeTests` | Navigation, Einstellungen, Store, Bibliothek, Detailseite, Dashboard, Speicherverwaltung, Downloads-Seite, Launcher-Update |
| `PackageTests` | Das Paket-Skript: ein gutes Paket besteht die Prüfung, sechs kaputte werden abgelehnt |

Der UI-Test läuft ohne sichtbares Fenster und legt Screenshots aller Seiten in
`build\release\screenshots` ab. Der Test `PackageTests` startet das gebaute Programm aus einem entpackten Paket
und braucht deshalb PowerShell.

## Release bauen

```powershell
.\scripts\package-portable.ps1 -Test
```

Das Skript baut, testet, stellt `KERJAMI-<Version>-Windows-x64-Portable.zip` samt Prüfsummendatei in `dist\`
zusammen und prüft das Paket von außen (Selbsttest des Programms, Manifest, keine Spur des Erstellers, der Launcher
nimmt es als Update an). Den ganzen Weg bis zum Release beschreibt
[docs/04-veroeffentlichen.md](docs/04-veroeffentlichen.md).

## Aufbau

```
cmake/Branding.cmake   Produktname, EXE-Name, GitHub-Koordinaten
src/app/               Startsequenz und Branding
src/core/              PathManager, SettingsManager, Logger, JSON-Dateien
src/github/            GitHub-Client, Registry, Releases
src/downloads/         Download-Warteschlange
src/security/          SHA-256, ZIP-Leser, Archiv-Prüfung
src/installer/         Installer, Deinstallation, Liste der Installationen, Start
src/process/           Prozessüberwachung
src/updater/           Update-Prüfung der Projekte, Selbstupdate des Launchers, Manifest, Austausch
src/models/            Datentypen
src/ui/                Hauptfenster, Seiten, Controls, Theme
resources/images/      Logo und Icons (erzeugt aus logo-source.png)
resources/registry/    Die Liste der freigegebenen Projekte (projects.json), in die EXE eingebaut
resources/styles/      Style Sheet
tests/                 Qt Test, über CTest
scripts/               Build, Paket (package-portable, verify-package, test-package), Icon-Erzeugung
.github/workflows/     CI und Release (noch nie gelaufen)
docs/                  Bestandsaufnahme, Plan, Entscheidungen
```

## Portabler Programmordner

```
KERJAMI_Launcher.exe
launcher/     Einstellungen und Launcher-Status
library/      installierte Projekte
downloads/    heruntergeladene Archive bis zur Installation
temp/         Teil-Downloads, Staging, entfernte Projekte vor dem Löschen, Launcher-Update
updater/      Journal, Backups und Bericht des Selbstupdates
cache/        Release-Cache
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
- [Ein Release veröffentlichen](docs/04-veroeffentlichen.md)
- [Die Projektliste und ihre Felder](docs/registry/README.md)
