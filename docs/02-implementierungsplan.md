# Implementierungsplan

Grundlage ist der Entwicklungsauftrag (Abschnitte 1–20). Dieser Plan legt Architektur,
Schnittstellen und die Reihenfolge fest. Stand der Phasen steht am Ende.

## Architektur

Drei Schichten, Abhängigkeiten zeigen nur nach unten:

```
launcher (EXE)        main.cpp, Application          Startsequenz, Fehlerdialoge
   │
launcher_ui (static)  MainWindow, Seiten, Controls   Qt Widgets, kein Netzwerk, keine Dateilogik
   │
launcher_core (static) Branding, PathManager,        Qt Core (ab Phase 2 zusätzlich Qt Network)
                       SettingsManager, Logger,
                       später: github/, downloads/,
                       installer/, updater/, security/, process/
```

`LauncherUpdater.exe` (Phase 6) linkt nur `launcher_core`. Die Tests linken `launcher_core`,
der UI-Test zusätzlich `launcher_ui`.

Regeln:

- Die UI ruft Manager auf und reagiert auf deren Signale. Sie öffnet keine Dateien und keine
  Netzwerkverbindungen.
- Netzwerk läuft asynchron über `QNetworkAccessManager`. Hashing, Entpacken und Austausch laufen in
  Worker-Threads (`QThread::create`), Ergebnisse kommen per Signal zurück. Geteilte Daten
  (`InstalledProjects`) sind thread-sicher.
- Alle Pfade kommen aus dem `PathManager`. Kein `QStandardPaths`, kein `QSettings`.
- Der Produktname steht nur in `cmake/Branding.cmake`. Bezeichner im Code sind neutral
  (`launcher::…`).

## Portable Verzeichnisstruktur

```
<Programmordner>/
├── KERJAMI.exe
├── LauncherUpdater.exe          Phase 6
├── launcher/
│   ├── settings.json            Benutzereinstellungen (Phase 1)
│   ├── launcher.json            Installationsstand, zuletzt gestartet (Phase 4)
│   └── launcher.lock            Einzelinstanz-Sperre
├── library/<install_directory>/
│   ├── current/                 aktive Installation
│   └── backups/<Version>/       die zwei zuletzt ersetzten Versionen für Rollback
├── downloads/<Projekt>/         heruntergeladene ZIPs, bis zur Installation
├── temp/                        download-<id>.part, staging-<Projekt>/, uninstall-<Projekt>/
├── cache/                       Registry-Cache, Release-Antworten
└── logs/                        launcher.log, updater.log
```

In JSON-Dateien stehen nur Pfade relativ zum Programmordner
(`PathManager::toPortable` / `fromPortable`).

## Schnittstellen

### Phase 1 (umgesetzt)

```cpp
namespace launcher::branding {          // Werte aus cmake/Branding.cmake
QString productName();  QString displayName();  QString version();
QString executableFileName();  QString updaterFileName();
QString gitHubOwner();  QString launcherRepository();  QUrl registryUrl();
}

class PathManager {                     // Wertklasse, kein Singleton
    static PathManager forApplication();
    explicit PathManager(const QString& rootDir);
    QString root(), configDir(), libraryDir(), downloadsDir(), tempDir(), cacheDir(), logsDir();
    QString settingsFile(), stateFile(), lockFile();
    PrepareResult prepare() const;                  // Ordner anlegen, Schreibrechte prüfen
    QString toPortable(const QString& absolute) const;
    QString fromPortable(const QString& portable) const;
    static bool isInside(const QString& baseDir, const QString& path);
};

class SettingsManager : public QObject {
    LoadResult load();                  // Loaded | CreatedDefaults | Migrated | Recovered…
    const LauncherSettings& settings() const;
    bool setStartPage(StartPage);  bool setRegistryAutoCheck(bool);
    bool setLauncherUpdateAutoCheck(bool);  bool setUpdateCheckIntervalMinutes(int);
signals:
    void settingsChanged();  void saveFailed(const QString& reason);
};

namespace JsonFile {                    // gemeinsame Basis für alle JSON-Dateien
LoadResult load(const QString& path);
bool saveAtomic(const QString& path, const QJsonObject& object, QString* error = nullptr);
QString backupAside(const QString& path, const QString& tag, BackupMode mode);
}

class Logger {                          // Qt-Message-Handler mit Rotation
    static bool install(const Options& options);
    static void uninstall();
};
```

### Phase 2 – Registry (umgesetzt)

```cpp
struct Project {                        // models/Project.h, nur validierte Werte
    QString id, name, description, owner, repository, category;
    QStringList tags;
    bool enabled, featured;
    ReleaseChannel releaseChannel;      // nur Stable in V1
    QString assetPattern, executable, installDirectory;
};

namespace RegistryParser {              // github/RegistryParser.h, reine Funktionen
RegistryParseResult parse(const QByteArray& json);
RegistryParseResult parse(const QJsonObject& root);    // auch für den Cache
bool isValidProjectId / isValidOwner / isValidRepository /
     isValidInstallDirectory / isValidAssetPattern / isValidExecutablePath(const QString&);
}

class GitHubClient : public QObject {   // einziger Ort mit QNetworkAccessManager
    struct Policy { QStringList schemes, hosts; bool anyPort;
                    static Policy production(); bool allows(const QUrl&) const; };
    GitHubRequest* get(const QUrl& url, const FetchOptions& options = {});
    RateLimit rateLimit() const;        // aus den X-RateLimit-Headern
signals: void rateLimitChanged(const RateLimit&);
};
class GitHubRequest : public QObject {  // eine laufende Anfrage, löscht sich selbst
    void abort();
signals: void finished(const NetworkResult& result);   // genau einmal, nie vor get()
};

class RegistryManager : public QObject {
    void loadCache();                   // beim Start, ohne Netzwerk
    void refresh();                     // lädt, validiert, schreibt den Cache
    void setAutoRefresh(bool enabled, int intervalMinutes);
    const QList<Project>& projects() const;            // nur enabled
    Source source() const;              // None | Network | Cache
    Problem problem() const;            // None | Unreachable | RateLimited | Http |
                                        // InvalidContent | UnsupportedVersion
signals: void changed();  void refreshFinished(bool success);
};
```

Die Oberfläche (`LibraryPage`) kennt nur den `RegistryManager`. Sie zeigt die Liste mit Suche,
Kategorie- und Tag-Filter und „Nur Empfohlene“, dazu Lade-, Offline- und Fehlerzustände.

Erlaubte Hosts (`Policy::production()`): `raw.githubusercontent.com`, `api.github.com`,
`github.com`, `objects.githubusercontent.com`, `release-assets.githubusercontent.com`, jeweils
nur über HTTPS auf dem Standardport. Die Entscheidungen dazu stehen in
[03-entscheidungen.md](03-entscheidungen.md), D-16 bis D-21.

### Phase 3 – Releases und Downloads (umgesetzt)

```cpp
class SemVer {                          // core/SemVer.h, Semantic Versioning 2.0.0
    int major, minor, patch;  QStringList preRelease;  QString build;
    static std::optional<SemVer> parse(const QString& text);   // "1.2.3", "v1.2.3", "1.0.0-rc.1+b5"
    friend std::strong_ordering operator<=>(const SemVer&, const SemVer&);   // build wird ignoriert
};

struct ReleaseAsset { QString name; qint64 size; QString sha256; };   // sha256 leer ohne GitHub-Digest
struct Release { QString tag; SemVer version; QString title, notes; QDateTime publishedAt;
                 QList<ReleaseAsset> assets; };
struct ReleaseSelection { Release release; ReleaseAsset asset; QUrl downloadUrl; };

namespace ReleaseParser {               // github/ReleaseParser.h, reine Funktionen
ReleaseListResult parseList(const QByteArray& json);
const Release* latest(const QList<Release>&);                      // höchste Version, nicht erste
QList<ReleaseAsset> matchingAssets(const Release&, const QString& pattern);
}

class ReleaseManager : public QObject {
    void fetchLatest(const Project& project);       // eine API-Anfrage je Projekt
    std::optional<ReleaseSelection> latest(const QString& projectId) const;
signals:
    void releaseReady(const QString& projectId, const ReleaseSelection& selection);
    void fetchFailed(const QString& projectId, Problem problem, int httpStatus);
};                                      // Problem: InvalidProject | Unreachable | RateLimited | Http |
                                        // InvalidContent | NoRelease | NoMatchingAsset | AmbiguousAsset

class DownloadManager : public QObject {            // Warteschlange, standardmäßig 2 parallel
    DownloadId enqueue(const DownloadRequest& request);
    void cancel(DownloadId id);   void clearFinished();
    QList<Download> downloads() const;
signals:
    void added(id);  void stateChanged(id);  void progressChanged(id);
    void finished(id);  void failed(id);  void removed(id);
};
```

`GitHubClient` kann den Körper einer Antwort direkt in eine Datei streamen
(`FetchOptions::saveToFile`) und meldet den Fortschritt aus den geschriebenen Bytes.

Ablauf eines Downloads: `temp/download-<id>.part` → Größe gegen die Angabe des Releases prüfen →
`downloads/<projekt>/<datei>`. Wiederholungen mit Backoff (höchstens drei Versuche, ein
unterbrochener Versuch beginnt die Datei neu), keine Wiederholung bei 4xx außer 429. Die
SHA-256-Prüfung folgt in Phase 4, bevor etwas entpackt wird. Details und Begründungen stehen in
[03-entscheidungen.md](03-entscheidungen.md), D-21 bis D-26.

### Phase 4 – Installation (umgesetzt)

```cpp
namespace HashVerifier {                // security/, reine Funktionen
bool isValidSha256(const QString& hex);
QString sha256File(const QString& path, QString* error, const std::atomic<bool>* cancel, const Progress&);
VerifyResult verifySha256(const QString& path, const QString& expected, cancel, progress);
QString findInChecksumFile(const QByteArray& content, const QString& fileName, bool allowBareHash);
}                                       // Status: Match | Mismatch | NoExpectedHash | InvalidExpectedHash | FileError | Cancelled

class ZipArchive {                      // security/, strenger Leser auf Qt-zlib (D-27)
    static OpenResult open(const QString& path);       // Error: CannotOpen | NotAZip | Unsupported | Corrupt
    const QList<Entry>& entries() const;               // Name, Art, Verfahren, Größen, CRC, Datenanfang
    ExtractResult extract(const Entry&, QIODevice& out, const std::atomic<bool>* cancel);
};
namespace ArchiveValidator {            // security/, D-29
ValidationResult validate(const ZipArchive&, const Limits&, const QString& requiredExecutable);
QString normalizeEntryPath(const QString& name, const Limits&, bool* isDirectory, QString* reason);
}                                       // Problem: Empty | TooManyEntries | TooLarge | UnsafePath | DuplicatePath |
                                        // ConflictingPath | UnsupportedEntry | CompressionBomb | MissingExecutable
namespace ArchiveExtractor { Result extract(ZipArchive&, const ValidationResult&, const QString& targetDir, cancel, progress); }

class InstalledProjects : public QObject {          // launcher/launcher.json, thread-sicher (D-31)
    LoadResult load();  bool contains(id);  optional<InstalledProject> get(id);  QList<InstalledProject> all();
    bool upsert(const InstalledProject&, QString* error);  bool remove(id, QString* error);
    void recordStart(id, QDateTime);
signals: void changed(const QString& projectId);
};
class LibraryLayout {                   // library/<dir>/current|backups, temp/staging-<id>, temp/uninstall-<id>
    projectDir(), currentDir(), backupsDir(), stagingDir(), trashDir(), executablePath();  int cleanupTemp() const;
};

class Installer : public QObject {      // D-30: Prüfen → Staging → Backup → Tausch → Eintrag, alles in einem Thread
    void install(const InstallRequest&);            // Project, Version, Tag, Archivpfad, SHA-256 (Pflicht)
    void cancel(const QString& projectId);          // bis zum Beginn des Austauschs
    void setRunningCheck(std::function<bool(const QString&)>);   // Phase 5 füllt das
signals:
    void progress(id, Stage, done, total);  void installed(id, InstallResult);  void failed(id, InstallResult);
};                                      // InstallError: Busy | InvalidProject | NoTrustedHash | HashMismatch |
                                        // ArchiveUnreadable | ArchiveRejected | InsufficientSpace | PathTooLong |
                                        // ExtractionFailed | ProjectRunning | SwapFailed | RegistrationFailed |
                                        // RollbackFailed | Cancelled
class Uninstaller : public QObject {    // D-32: erst nach temp/ verschieben, dann im Hintergrund löschen
    UninstallResult uninstall(const QString& projectId);
signals: void uninstalled(id);  void cleanupFinished(id, bool complete);
};
StartResult startProject(const LibraryLayout&, InstalledProjects&, const QString& projectId);   // D-33

class InstallService : public QObject { // D-34: die Kette hinter dem Knopf „Installieren“
    void install(const Project&);       // Release → Hash-Quelle → Download → Installer
    void cancel(id);  UninstallResult uninstall(id);  StartResult start(id);
    ProjectStatus status(id);  OperationFailure lastFailure(id);
signals: void statusChanged(id);  void installFinished(id, bool success);
};
```

`ReleaseSelection` trägt zusätzlich `checksumFile` (Name, Adresse, ob ein nackter Hash genügt), wenn
GitHub keinen Digest meldet und das Release genau eine passende Prüfsummendatei hat
(`ReleaseParser::checksumAsset`).

Die Projektkarte der Bibliothek zeigt Zustand, Fortschritt und Fehler und ruft den Dienst auf
(`ui/ProjectCard`, Meldungstexte in `ui/OperationMessages`, Bestätigung in `ui/Dialogs`).

Der Start räumt übrig gebliebene `staging-*` und `uninstall-*` aus `temp/` auf. Alle Entscheidungen
und Grenzen stehen in [03-entscheidungen.md](03-entscheidungen.md), D-27 bis D-34.

### Phase 5 – Projekt-Updates

```cpp
class ProcessManager : public QObject { // QProcess plus Job-Objekt für Kindprozesse
    bool start(const Project& project);  bool isRunning(const QString& projectId) const;
signals: void started(const QString& projectId);  void exited(const QString& projectId, int code);
};
class ProjectUpdateManager : public QObject {
    UpdateState state(const QString& projectId) const;  // UpToDate | Available | Downloading | Pending | …
    void checkAll();  void applyUpdate(const QString& projectId);
};
```

Pro-Projekt-Einstellungen (`update_mode`) kommen als Abschnitt `projects` in `settings.json`.

### Phase 6 – Selbstupdate

`LauncherUpdateManager` lädt und prüft das Release-ZIP und entpackt es nach `temp/`. Danach
kopiert er `LauncherUpdater.exe` nach `temp/updater-<version>/` und startet diese Kopie. Der
Updater wartet auf das Prozessende des Launchers (Handle auf die PID), sichert die Dateien aus
dem Manifest des Releases, tauscht sie und startet den Launcher neu. Weil die laufende
Updater-Kopie außerhalb der Programmdateien liegt, kann auch `LauncherUpdater.exe` selbst
ersetzt werden. Der Updater fasst nur Dateien an, die im Manifest stehen.

### Phase 7 und 8

UI-Fertigstellung nach Abschnitt 11, danach Paketierung (`package-portable.ps1`,
`verify-package.ps1`), GitHub Actions und Dokumentation.

## Tests

Qt Test über CTest. Logik wird ohne Netzwerk getestet: Parser und Validatoren sind reine
Funktionen, Netzwerk-Tests laufen gegen einen lokalen `QTcpServer` im Testprozess.

| Datei | Inhalt | Phase |
|---|---|---|
| `PortableTests.cpp` | Pfade, Verschieben, Schreibrechte | 1 |
| `SettingsTests.cpp` | Persistenz, Korruption, Migration | 1 |
| `LoggerTests.cpp` | Rotation, Größenlimit, Pfad-Schwärzung | 1 |
| `UiSmokeTests.cpp` | Navigation, Einstellungen, Bibliothek, Downloads-Seite | 1–3 |
| `RegistryTests.cpp` | Parser: alle Prüfregeln, Duplikate, Grenzen, Pfad- und Namensregeln | 2 |
| `GitHubClientTests.cpp` | URL-Policy, Wiederholungen, Rate Limit, Größe, Redirects, Abbruch, Speichern in Datei | 2–3 |
| `RegistryManagerTests.cpp` | Laden, Cache, Offline-Verhalten, beschädigter Cache | 2 |
| `SemVerTests.cpp` | Gültige und ungültige Versionen, Reihenfolge nach der SemVer-Spezifikation | 3 |
| `ReleaseTests.cpp` | Release-Parser, Asset-Auswahl, Digest, Prüfsummendatei, ReleaseManager (Fehler, Rate Limit, Duplikate) | 3–4 |
| `DownloadTests.cpp` | Tempo, Formatierung, Warteschlange, Abbruch, Größen- und Speicherfehler | 3 |
| `SecurityTests.cpp` | SHA-256, Prüfsummendatei, ZIP-Leser (kaputte und feindliche Archive), Pfadregeln, Validator, Extraktor | 4 |
| `InstallerTests.cpp` | Installation, Update mit Backup, Rollback, Deinstallation, Liste der Installationen, laufendes Programm, Links | 4 |
| `InstallServiceTests.cpp` | Die ganze Kette gegen einen lokalen Server: Hash-Quellen, falsche Hashes, feindliche Archive, Abbruch | 4 |
| `UpdateTests.cpp`, Selbstupdate-Tests | | 5–6 |

## Stand

| Phase | Inhalt | Status |
|---|---|---|
| 1 | Grundgerüst | umgesetzt |
| 2 | Registry | umgesetzt und gegen GitHub geprüft; die echte Registry fehlt noch (E-03), bis dahin zeigt die Bibliothek „nicht gefunden (HTTP 404)“ |
| 3 | Releases und Downloads | umgesetzt und gegen GitHub geprüft (nur Metadaten); ein Download-Knopf fehlt bis Phase 4 |
| 4 | Installation | umgesetzt und gegen einen lokalen Server und echte Dateisystem-Fälle geprüft (gesperrte Dateien, Junctions); ein Test mit echten GitHub-Releases fehlt noch, er braucht ein Testprojekt mit ZIP-Releases (E-02) |
| 5 | Projekt-Updates | offen |
| 6 | Launcher-Selbstupdate | offen; braucht Releases in `KERSEX/KERJAMI_Launcher` |
| 7 | UI-Fertigstellung | offen |
| 8 | Portable Release | offen; braucht Entscheidung E-01 |

Offene Entscheidungen stehen in [03-entscheidungen.md](03-entscheidungen.md):

- E-01 Compiler und Qt-Variante (auch wegen der Qt-Lizenz, E-06)
- E-02 Testprojekt mit ZIP-Releases für den Test in Phase 4
- E-03 Registry-Repository (Name, Datei im Repository oder Release-Asset)
- E-05 Theme-Auswahl, E-06 Umfang der GitHub-Veröffentlichung

Die Reihenfolge der Phasen steht fest. Phase 2 und 3 laufen gegen einen lokalen Testserver und
hängen von keiner dieser Entscheidungen ab.
