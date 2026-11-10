# Technische Bestandsaufnahme

Stand: 2026-11-09, Entwicklungsrechner Windows 11 (10.0.26300).

## Repository

Das Arbeitsverzeichnis `D:\Programme\Code\KERJAMI_Launcher` war leer und kein Git-Repository.
Das Projekt startet ohne Altcode.

## Entwicklungsumgebung

| Werkzeug | Gefunden | Ort |
|---|---|---|
| Qt | 6.11.2, **statisch**, Release | `D:\Qt\6.11.2-static` |
| Compiler | clang 23.1.2 (llvm-mingw, `x86_64-w64-windows-gnu`) | `D:\Tools\llvm-mingw` |
| CMake | 4.4.4 | `D:\Tools\cmake` |
| Ninja | 1.13.2 | `D:\Tools\ninja` |
| Git | 2.55.0 | `E:\Git` |
| Windows SDK | 10.0.26100 | Standardpfad |
| MSVC 2022 | **nicht installiert** | – |
| GitHub CLI | nicht installiert | – |

Keines der Build-Werkzeuge liegt im `PATH`. `scripts/build.ps1` löst die Pfade deshalb selbst auf.

### Eigenschaften des Qt-Builds

- Aus den Quellen gebaut (`D:\Qt\src`, Build-Verzeichnisse unter `D:\Qt\b`), mkspec `win32-clang-g++`.
  Laut Team stammt dieser Build aus dem Projekt KERS Overlay.
- Statische Bibliotheken und statische Laufzeit (`FEATURE_static_runtime=ON`).
- TLS über **Schannel** (Windows-Bordmittel), kein OpenSSL.
- Vorhandene Module: Core, Gui, Widgets, Network, Concurrent, Test, Xml, Sql, dazu QML/Quick und WebSockets.
- **Nicht vorhanden: QtSvg.** Icons können nicht als SVG geladen werden.
- Plattform-Plugins: `qwindows`, `qoffscreen`, `qminimal`, `qdirect2d`.
- Nur Release-Bibliotheken, keine Debug-Variante.

## Abweichungen vom Auftrag

### 1. Compiler: llvm-mingw statt MSVC 2022

Der Auftrag legt MSVC 2022 fest. Auf dem Rechner gibt es kein MSVC, und das vorhandene Qt ist
nicht mit MSVC-Binaries kompatibel. Phase 1 wurde deshalb mit llvm-mingw gebaut.

Folgen des statischen Qt-Builds:

- `KERJAMI.exe` enthält Qt vollständig. Es gibt keine Qt-DLLs und keine Plugin-Ordner.
- `windeployqt` und `qt.conf` werden für dieses Setup nicht gebraucht.
- Das Portable-ZIP besteht im Kern aus zwei EXE-Dateien.

Der Quellcode bleibt compilerneutral (C++20, CMake, MSVC-Zweig in den Compiler-Optionen), ist
aber bisher **nur mit clang gebaut und getestet**.

Offene Entscheidung: bei llvm-mingw bleiben oder MSVC 2022 plus MSVC-Qt installieren.
Siehe `03-entscheidungen.md`, E-01.

### 2. GitHub-Repositories

| Repository | Status am 2026-11-09 |
|---|---|
| `KERSEX/KERJAMI_Launcher` | öffentlich, Branch `main`, nur eine README (Repository des Launchers) |
| Registry-Repository | existiert nicht; Name offen |
| `KERSEX/CommunityLauncher-Registry` | 404 (Name aus dem Auftrag, wird nicht verwendet, solange er nicht angelegt ist) |
| `KERSEX/KERS_Overlay` | öffentlich, Branch `main` (anderes Projekt, siehe Punkt 3) |

Der Auftrag nannte `KERSEX/CommunityLauncher` als Launcher-Repository. Das Team hat stattdessen
`KERJAMI_Launcher` angelegt, `cmake/Branding.cmake` zeigt darauf. Die Registry-URL liefert 404.
Phase 2 braucht das Registry-Repository mit einer `projects.json`, Phase 6 braucht Releases im
Launcher-Repository. Beide müssen öffentlich sein, weil der Launcher ohne Anmeldung arbeitet.

### 3. KERS Overlay ist nur ein Beispielprojekt

Laut Team ist `KERSEX/KERS_Overlay` ein anderes Projekt und dient nur als Beispiel für etwas, das
später über den Launcher geladen und installiert wird. Es ist kein Teil des Launchers und kein
verbindliches Testprojekt. Der Auftrag fordert ZIP-Releases; das bleibt die Grundlage für
Version 1 (siehe `03-entscheidungen.md`, D-14).

Zur Einordnung die drei neuesten Releases von `KERSEX/KERS_Overlay`:

| Tag | Asset | Größe | SHA-256-Digest |
|---|---|---|---|
| `KERS_SubsystemsV0.3.1` | `KERS_Subsystems.exe` | 56,2 MB | vorhanden |
| `KERS_SubsystemsV0.3.0` | `KERS_Subsystems.exe` | 55,2 MB | vorhanden |
| `KERS_SubsystemsV0.2.6` | `KERS_Subsystems.exe` | 232,2 MB | vorhanden |

Das Registry-Beispiel im Auftrag erwartet `KERS_Overlay-Windows-x64.zip` mit `KERS_Overlay.exe`.
Tatsächlich gibt es ein einzelnes EXE-Asset, einen Tag mit Präfix (`KERS_SubsystemsV0.3.1`) und den
Dateinamen `KERS_Subsystems.exe`. Die GitHub-Asset-Digests sind vorhanden, GitHub liefert den
SHA-256 also mit.

Für den Ende-zu-Ende-Test in Phase 4 wird deshalb ein eigenes Testprojekt mit ZIP-Release
gebraucht (siehe `03-entscheidungen.md`, E-02).

### 4. ZIP-Bibliothek

Weder libarchive noch QuaZip sind installiert. Beide hätten für Phase 4 mit llvm-mingw gebaut und
statisch gelinkt werden müssen. Entschieden in Phase 4: ein eigener, strenger ZIP-Leser auf dem
zlib, das in Qt steckt (`03-entscheidungen.md`, D-27). Es kommt keine zusätzliche Bibliothek hinzu.

## Netzwerk

`api.github.com` ist erreichbar. Ohne Anmeldung gilt das Limit von 60 Anfragen pro Stunde je
IP-Adresse. Das Prüfintervall und die Zahl der Projekte müssen dazu passen (ein Release-Abruf
je Projekt und Prüfung).
