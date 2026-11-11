# Implementierungsplan

Grundlage ist der Entwicklungsauftrag (Abschnitte 1–20). Dieser Plan legt Architektur,
Schnittstellen und die Reihenfolge fest. Stand der Phasen steht am Ende.

## Architektur

Vier Schichten, Abhängigkeiten zeigen nur nach unten:

```
launcher (EXE)         main.cpp, Application          Startsequenz, Fehlerdialoge
   │
launcher_ui (static)   MainWindow, Seiten, Controls   Qt Widgets, kein Netzwerk, keine Dateilogik
   │
launcher_core (static) Branding, SettingsManager,     Qt Core + Network: github/, downloads/,
                       Manager und Dienste            installer/, process/, updater/
   │
launcher_base (static) PathManager, Logger, JsonFile, Qt Core: alles, was auch der Austausch der
                       SemVer, FileOps, HashVerifier,  Programmdateien braucht
                       ArchiveValidator, RegistryParser,
                       UpdateManifest, UpdateTransaction,
                       UpdaterProtocol, LauncherUpdater
```

Es gibt nur **eine EXE**: Der Austausch der Programmdateien beim Selbstupdate (Phase 6) ist eine
Betriebsart von `KERJAMI_Launcher.exe` (`--apply-update`) und liegt in `launcher_base`: kein Netzwerk, keine Fenster.
Die Tests linken `launcher_core`, der UI-Test zusätzlich `launcher_ui`.

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
├── KERJAMI_Launcher.exe
├── launcher/
│   ├── settings.json            Benutzereinstellungen (Phase 1)
│   ├── launcher.json            Installationsstand, zuletzt gestartet (Phase 4)
│   └── launcher.lock            Einzelinstanz-Sperre
├── library/<install_directory>/
│   ├── current/                 aktive Installation
│   └── backups/<Version>/       die zwei zuletzt ersetzten Versionen für Rollback
├── downloads/<Projekt>/         heruntergeladene ZIPs, bis zur Installation
├── downloads/launcher/          das Release-ZIP des Launchers, bis es geprüft ist (Phase 6)
├── temp/                        download-<id>.part, staging-<Projekt>/, uninstall-<Projekt>/,
│                                update-<Version>/ (entpacktes Launcher-Release),
│                                updater-<Version>/ (die laufende Kopie des Launchers für den Austausch)
├── updater/                     Journal, Backups der ersetzten Programmdateien, Bericht, Sperre (Phase 6)
├── cache/                       Release-Antworten
└── logs/                        launcher.log, updater.log
```

In JSON-Dateien stehen nur Pfade relativ zum Programmordner
(`PathManager::toPortable` / `fromPortable`).

## Schnittstellen

### Phase 1 (umgesetzt)

```cpp
namespace launcher::branding {          // Werte aus cmake/Branding.cmake
QString productName();  QString displayName();  QString version();
QString executableFileName();  QString updateAssetPattern();
QString gitHubOwner();  QString launcherRepository();
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
    bool setStartPage(StartPage);
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

### Phase 2 – Registry (umgesetzt; seit D-43 eine eingebaute Liste)

```cpp
struct Project {                        // models/Project.h, nur validierte Werte
    QString id, name, description, owner, repository, category;
    QStringList tags;
    bool enabled, featured;
    ReleaseChannel releaseChannel;      // nur Stable in V1
    QString tagPrefix, assetPattern, executable, installDirectory;   // tagPrefix optional (D-42)
};

namespace RegistryParser {              // github/RegistryParser.h, reine Funktionen
RegistryParseResult parse(const QByteArray& json);
RegistryParseResult parse(const QJsonObject& root);
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

class RegistryManager : public QObject {   // D-43: die Liste steckt im Programm
    bool loadBuiltIn();                 // liest :/registry/projects.json, beim Start
    bool load(const QByteArray& json);  // Tests; ein unbrauchbares Dokument lässt die Liste, wie sie war
    const QList<Project>& projects() const;            // nur enabled, in der Reihenfolge der Datei
    const QList<RegistryIssue>& issues() const;        // verworfene Einträge mit Grund
    QString error() const;              // warum das letzte Laden scheiterte
signals: void changed();
};
```

Die Oberfläche (`StorePage`, seit D-48) kennt nur den `RegistryManager`. Sie zeigt die Liste als Kacheln
mit Suche und Kategorie-Auswahl, dazu die Zustände leere Liste und keine Treffer.
Die Datei der Liste ist `resources/registry/projects.json`; die Feldreferenz steht in
[registry/README.md](registry/README.md).

Erlaubte Hosts (`Policy::production()`): `api.github.com`, `github.com`,
`objects.githubusercontent.com`, `release-assets.githubusercontent.com`, jeweils
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

Die Kachel im Store und die Karte der Bibliothek zeigen Zustand, Fortschritt und Fehler und rufen den
Dienst auf (`ui/StoreTile`, `ui/ProjectCard`, Meldungstexte in `ui/OperationMessages`, Bestätigung in
`ui/Dialogs`).

Der Start räumt übrig gebliebene `staging-*` und `uninstall-*` aus `temp/` auf. Alle Entscheidungen
und Grenzen stehen in [03-entscheidungen.md](03-entscheidungen.md), D-27 bis D-34.

### Phase 5 – Projekt-Updates (umgesetzt)

```cpp
enum class UpdateMode { Manual, Automatic };      // models/ProjectSettings.h
class SettingsManager {                           // Abschnitt "projects" in settings.json (D-38)
    UpdateMode updateMode(const QString& projectId) const;       // Manual ohne Eintrag
    bool setUpdateMode(const QString& projectId, UpdateMode);    // sofort gespeichert
    void removeProjectSettings(const QString& projectId);
signals: void projectSettingsChanged(const QString& projectId);
};

class ProcessManager : public QObject {           // process/, D-35
    StartResult start(const QString& projectId);              // Job-Objekt; AlreadyRunning bei Mehrfachstart
    bool isRunning(id) const;                                 // Stand der letzten Abfrage, für die UI
    bool isRunningNow(id) const;                              // frisch, aus jedem Thread
    void refresh();                                           // alle 2 s; Tests rufen es selbst
    optional<ProcessExit> lastExit(id) const;                 // Exitcode, crashed(), unexpected()
signals: void started(id);  void runningChanged(id, bool);  void exited(id, ProcessExit);
};

class InstallService {                            // Phase 4, erweitert
    enum class Mode { InstallNow, PrepareOnly };
    void install(const Project&, Mode = Mode::InstallNow);    // PrepareOnly: laden + prüfen, dann warten
    void installPrepared(const QString& projectId);           // geht mit dem Vorbereiteten weiter
    void setStarter(std::function<StartResult(const QString&)>);   // die Anwendung setzt ProcessManager::start
signals: void prepared(id);  // plus Stage::Prepared
};

enum class UpdateState { Unknown, UpToDate, Available, Queued, Downloading, Verifying, Pending, Installing, Failed };
class ProjectUpdateManager : public QObject {     // updater/, D-36 und D-37
    void start();  void checkAll();                           // Intervall aus den Einstellungen
    UpdateInfo info(const QString& projectId) const;          // Zustand, Versionen, Changelog, Fehler
    void applyUpdate(id);  void cancel(id);                   // Entscheidung des Nutzers
    QStringList queue() const;  QStringList active() const;
signals: void updateChanged(id);  void checkFinished();
};
```

Die Anwendung verbindet die Teile: `Installer` und `Uninstaller` fragen vor jedem Eingriff den
`ProcessManager` (`setRunningCheck(isRunningNow)`), der Dienst startet über ihn, und der
Update-Manager entscheidet je Projekt nach dem Modus. Die Oberfläche bekommt alle Manager gebündelt
in einem `ui::AppContext`.

Die Projektkarte zeigt das Update-Badge („Update 2.0.0“), „Aktualisieren“ mit Rückfrage und
Changelog, „Updates: Manuell/Automatisch“, den Zustand „läuft“ und unerwartete Programmenden
(`ui/ProjectCard`, `ui/Dialogs`, `ui/OperationMessages`).

Der Start löscht außerdem übrig gebliebene Archive in `downloads/<Projekt>/`. Entscheidungen und
Grenzen: [03-entscheidungen.md](03-entscheidungen.md), D-35 bis D-38.

### Phase 6 – Selbstupdate (umgesetzt)

Das Release-Asset des Launchers heißt `KERJAMI-<Version>-Windows-x64-Portable.zip`
(`branding::updateAssetPattern()`). Es enthält `KERJAMI_Launcher.exe` an der Wurzel und die Datei
`update-manifest.json` (D-39). Das Programm ist eine einzige Datei.

```cpp
enum class LauncherUpdateState { Idle, Checking, UpToDate, Available, Downloading, Preparing, Ready, Applying, Failed };
class LauncherUpdateManager : public QObject {    // updater/, D-41
    void start();  void checkNow();                           // erste Prüfung nach 3 s, dann im Intervall
    LauncherUpdateInfo info() const;                          // Zustand, Versionen, Fortschritt, Fehler
    void install();                                           // laden, prüfen, entpacken; danach folgt apply()
    void cancel();
    void apply();                                             // Kopie des Launchers starten, Launcher beendet sich
    void setBusyCheck(std::function<bool()>);                 // laufende Installationen blockieren den Neustart
signals: void infoChanged();  void restartRequested();
};

struct PackageRequest { /* ZIP, Hash, Staging-Ordner, Erwartungen */ };
namespace UpdatePackage { PackageResult prepare(const PackageRequest&, cancel, progress); }   // D-39

// launcher_base, gemeinsam mit dem Austausch
namespace UpdateManifestFormat { ManifestParseResult parse(json, ManifestExpectations); }     // D-39
namespace UpdateTransaction {                                                                  // D-40
    TransactionResult apply(const TransactionPlan&);                  // Journal, Backup, Austausch, Rollback
    TransactionResult recover(const QString& rootDir, const QString& journalFile);
}
struct UpdaterJob { /* Ordner, Versionen, Dateiname, PID des Launchers, Wartezeit */ };         // job.json
struct UpdateReport { /* Status, Versionen, Detail, Backup */ };                               // last-update.json
namespace UpdateStartup { StartupUpdateOutcome run(const PathManager&, int waitMs = 60000); }  // beim Start des Launchers

// KERJAMI_Launcher.exe --apply-update --job <job.json> [--quiet]   (LauncherUpdater::runProgram, vor jedem Fenster)
// Exitcodes: 0 Erfolg, 1 zurückgerollt, 2 nichts geändert, 3 fehlgeschlagen, 4 ungültiger Auftrag
```

Ablauf: Prüfung über dieselben Release-Abfragen wie bei Projekten (reservierte ID `launcher`, D-39) →
„Aktualisieren“ mit Rückfrage (Changelog) → Download nach `downloads/launcher/` → SHA-256 →
Archivprüfung → Entpacken nach `temp/update-<Version>/` → Manifest und Hash jeder Datei prüfen →
Zustand „bereit“ → `apply()`: den laufenden Launcher nach `temp/updater-<Version>/` kopieren,
`job.json` schreiben, die Kopie mit `--apply-update` starten, Launcher beenden. `apply()` folgt von
selbst, sobald das Paket bereit ist und keine Projekt-Installation läuft; sonst wartet es auf „Jetzt
neu starten“. Die Kopie wartet auf das Prozessende des Launchers (Handle auf die PID), tauscht die
Dateien aus dem Manifest in einer Transaktion (Journal, Backups, Rollback; die Programmdatei des
Launchers zuletzt), schreibt `updater/last-update.json` und startet den Launcher neu. Weil die laufende
Kopie in `temp/` liegt, kann die Datei `KERJAMI_Launcher.exe` im Programmordner ersetzt werden. Der Austausch
fasst nur Dateien an, die im Manifest stehen; die Ordner mit Nutzerdaten sind gesperrt. Beim nächsten
Start räumt der Launcher auf: `UpdateStartup::run` wartet auf einen noch laufenden Austausch, schließt
eine unterbrochene Transaktion ab (`recover`) und zeigt den Bericht einmal an.

Die Oberfläche zeigt den Stand auf dem Dashboard und in „Einstellungen → Über“
(`ui/LauncherUpdatePanel`): „Nach Updates suchen“, „Aktualisieren“ mit Rückfrage, „Abbrechen“ und
„Jetzt neu starten“ (nur wenn eine Installation den Neustart aufgehalten hat). Entscheidungen und Grenzen: [03-entscheidungen.md](03-entscheidungen.md), D-39 bis D-41.

### Phase 7 – UI-Fertigstellung (umgesetzt)

```cpp
class ProjectControls : public QWidget {          // ui/, gemeinsam für Kachel, Karte und Detailseite
    enum class Mode { Full, Library, Store };     // wie viel davon ein Ort zeigt (D-48)
    ProjectControls(const Project&, const AppContext&, Mode, QWidget* parent);
    void addLeadingButton(QPushButton*);          // Installieren/Starten/Aktualisieren/Deinstallieren,
};                                                // Updates: Manuell/Automatisch, Status, Fortschritt, Fehler

class ProjectDetailPage : public Page {           // nicht in der Seitenleiste
    void showProject(const Project&);             // fragt GitHub, wenn 10 Minuten nichts bekannt ist
signals: void backRequested();
};
class MainWindow { void showProject(const QString& projectId); };   // PageId::ProjectDetail

namespace Storage {                               // installer/Storage.h
    struct Usage { qint64 downloadsAndTemp, backups; };
    Usage measure(const PathManager&, const std::atomic<bool>* cancel = nullptr);
    bool clearDownloadsAndTemp(const PathManager&, QString* error = nullptr);
    bool clearBackups(const PathManager&, QString* error = nullptr);
}
```

`ui::AppContext` bekommt den `ReleaseManager` für die Detailseite. Das Dashboard zeigt Updates und die Projekte,
die zuletzt verwendet wurden. Die Einstellungen bekommen die Speicherverwaltung. Die Oberfläche wurde danach
neu geordnet (D-48): **Store** (alles Installierbare, als Kacheln), **Bibliothek** (nur Installiertes), Dashboard
und Detailseite. Dazu
kommt die Installation einer **einzelnen EXE als Release** (`RegistryParser::isSingleFileAsset`,
`InstallError::NotAProgram`) und das Feld **`keep`** im Registry-Eintrag, das Daten eines Projekts (Einstellungen)
bei einem Update in die neue Version übernimmt (`FileOps::copyTree`, `InstallError::CarryOverFailed`).
Entscheidungen: [03-entscheidungen.md](03-entscheidungen.md), D-44, D-45 und D-47.

### Phase 8 – Portable Release (umgesetzt)

```
scripts/package-portable.ps1   Bauen, Dateien zusammenstellen, Selbsttest, Manifest, ZIP, Prüfsumme, Prüfung
scripts/verify-package.ps1     Prüfung eines Pakets von außen (12 Prüfungen), Exitcode 0 oder 1
scripts/test-package.ps1       CTest PackageTests: gutes Paket besteht, sechs kaputte werden abgelehnt
.github/workflows/ci.yml       Bauen und Testen bei jedem Push (nie gelaufen)
.github/workflows/release.yml  Tag v<Version> → Paket → Entwurf-Release (nie gelaufen)
docs/04-veroeffentlichen.md    Schritt für Schritt zum Release
```

```cpp
class SelfTest {                                  // app/SelfTest.h, ohne Fenster, ohne Netzwerk
    // KERJAMI_Launcher.exe --self-test <Ergebnis.json>
    // KERJAMI_Launcher.exe --check-package --zip F --sha256 H --version V --work D --out Ergebnis.json
    static bool isRequested(int argc, char* argv[]);
    static int runProgram(int argc, char* argv[]);
};
```

Das Paket ist `KERJAMI-<Version>-Windows-x64-Portable.zip` mit `KERJAMI_Launcher.exe` und `update-manifest.json`
(mit dem statischen Qt) und `….zip.sha256`. Entscheidungen und offene Punkte: [03-entscheidungen.md](03-entscheidungen.md), D-46.

## Tests

Qt Test über CTest. Logik wird ohne Netzwerk getestet: Parser und Validatoren sind reine
Funktionen, Netzwerk-Tests laufen gegen einen lokalen `QTcpServer` im Testprozess.

| Datei | Inhalt | Phase |
|---|---|---|
| `PortableTests.cpp` | Pfade, Verschieben, Schreibrechte | 1 |
| `SettingsTests.cpp` | Persistenz, Korruption, Migration, Einstellungen pro Projekt | 1, 5 |
| `LoggerTests.cpp` | Rotation, Größenlimit, Pfad-Schwärzung | 1 |
| `UiSmokeTests.cpp` | Navigation, Einstellungen, Store (Kacheln, Suche, Kategorien, kleinstes Fenster), Bibliothek (Karten, Update, läuft, Projekt außerhalb der Liste), Downloads-Seite, Anzeige des Launcher-Updates, Detailseite (Deinstallieren, Update-Modus), Dashboard, Speicherverwaltung | 1–7 |
| `RegistryTests.cpp` | Parser: alle Prüfregeln, Duplikate, Grenzen, Pfad- und Namensregeln, Tag-Präfix | 2, 6 |
| `GitHubClientTests.cpp` | URL-Policy, Wiederholungen, Rate Limit, Größe, Redirects, Abbruch, Speichern in Datei | 2–3 |
| `RegistryManagerTests.cpp` | Die eingebaute Liste ist im Programm und gültig, Laden, unbrauchbare Dokumente lassen die Liste unverändert | 2, 6 |
| `SemVerTests.cpp` | Gültige und ungültige Versionen, Reihenfolge nach der SemVer-Spezifikation | 3 |
| `ReleaseTests.cpp` | Release-Parser, Asset-Auswahl, Digest, Prüfsummendatei, Tag-Präfix, ReleaseManager (Fehler, Rate Limit, Duplikate) | 3–4, 6 |
| `DownloadTests.cpp` | Tempo, Formatierung, Warteschlange, Abbruch, Größen- und Speicherfehler | 3 |
| `SecurityTests.cpp` | SHA-256, Prüfsummendatei, ZIP-Leser (kaputte und feindliche Archive), Pfadregeln, Validator, Extraktor | 4 |
| `InstallerTests.cpp` | Installation, Update mit Backup, Rollback, Deinstallation, Liste der Installationen, laufendes Programm, Links, einzelne EXE als Release | 4, 7 |
| `InstallServiceTests.cpp` | Die ganze Kette gegen einen lokalen Server: Hash-Quellen, falsche Hashes, feindliche Archive, Abbruch | 4 |
| `ProcessTests.cpp` | Start, Mehrfachstart, Ende und Exitcode, Programm von außen gestartet, Nachkommen außerhalb des Ordners, Threads (mit dem Hilfsprogramm `SleepyApp`) | 5 |
| `UpdateTests.cpp` | Prüfung, manuell und automatisch, laufendes Projekt, Vormerkung, Fehler, Warteschlange, Modi je Projekt, Netzwerkfehler | 5 |
| `StorageTests.cpp` | Größen von Downloads, temporären Dateien und Sicherungen, Aufräumen, nie durch einen Link | 7 |
| `PackageTests` (`scripts/test-package.ps1`) | Paket bauen, von außen prüfen, sechs kaputte Pakete ablehnen | 8 |
| `UpdateTransactionTests.cpp` | Manifest-Parser (Tabelle feindlicher Manifeste), Transaktion: Erfolg, Ablehnungen, Rollback bei jedem Schritt, Absturz nach jedem Schritt und `recover`, `UpdateStartup` | 6 |
| `UpdaterTests.cpp` | Die echte `KERJAMI_Launcher.exe` im Modus `--apply-update` mit dem Hilfsprogramm `SleepyApp` als Launcher: Austausch, Warten auf das Prozessende, Ablehnung, Bericht, Neustart, Sperre | 6 |
| `LauncherUpdateTests.cpp` | Prüfung und Zustände, feindliche Pakete, Abbruch, Busy, Übergabe an den Austausch, Selbstupdate von Anfang bis Ende gegen einen lokalen Server | 6 |

## Stand

| Phase | Inhalt | Status |
|---|---|---|
| 1 | Grundgerüst | umgesetzt |
| 2 | Registry | umgesetzt; seit D-43 steckt die Liste im Programm (kein Abruf, kein Cache). Sie ist noch leer, ihr Inhalt hängt an Angaben des Teams (E-07) |
| 3 | Releases und Downloads | umgesetzt und gegen GitHub geprüft (nur Metadaten); ein Download-Knopf fehlt bis Phase 4 |
| 4 | Installation | umgesetzt und gegen einen lokalen Server und echte Dateisystem-Fälle geprüft (gesperrte Dateien, Junctions); echt gegen GitHub getestet ist die Installation einer einzelnen EXE (KERS Overlay, 2026-11-11); mit einem ZIP-Release fehlt der Test noch (E-02) |
| 5 | Projekt-Updates | umgesetzt, gegen einen lokalen Server und echte Prozesse geprüft; ein Update zwischen zwei echten Versionen ist nicht gegen GitHub getestet (E-02) |
| 6 | Launcher-Selbstupdate | umgesetzt, mit der echten `KERJAMI_Launcher.exe` im Austausch-Modus gegen einen lokalen Server geprüft; ein Test gegen ein echtes Release in `KERSEX/KERJAMI_Launcher` fehlt, das Repository hat noch keines. Das Programm ist eine einzige Datei (D-40) |
| 7 | UI-Fertigstellung | umgesetzt: Detailseite, Dashboard, Speicherverwaltung, Einzel-EXE-Projekte; danach neu geordnet in Store, Bibliothek und Dashboard (D-48); getestet mit Screenshots; das Theme bleibt dunkel (E-05) |
| 8 | Portable Release | umgesetzt für das statische Qt (Paket, Prüfung, Selbsttest); GitHub Actions, MSVC und ein geteiltes Qt sind geschrieben, aber nie gelaufen; Lizenzhinweise und Signatur fehlen (E-01, D-46) |

Offene Entscheidungen stehen in [03-entscheidungen.md](03-entscheidungen.md):

- E-01 Compiler und Qt-Variante (auch wegen der Qt-Lizenz, E-06)
- E-02 Testprojekt mit ZIP-Releases für den Test in Phase 4
- E-05 Theme-Auswahl, E-06 Umfang der GitHub-Veröffentlichung

Alle acht Phasen sind umgesetzt. Bis zu einem öffentlichen Release bleibt: E-01 entscheiden, die Lizenzhinweise
klären, die Workflows einmal laufen lassen und die Einträge der Projektliste vervollständigen.

Die Reihenfolge der Phasen stand fest. Phase 2 und 3 laufen gegen einen lokalen Testserver und
hängen von keiner dieser Entscheidungen ab.
