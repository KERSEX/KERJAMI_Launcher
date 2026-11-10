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
- Netzwerk läuft asynchron über `QNetworkAccessManager`. Hashing und Entpacken laufen in
  Worker-Threads (`QtConcurrent`), Ergebnisse kommen per Signal zurück.
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
│   └── backups/                 vorherige Version für Rollback
├── downloads/                   fertige, geprüfte Downloads
├── temp/                        Teil-Downloads, Staging
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

### Phase 2 – Registry

```cpp
struct Project {                        // models/Project.h
    QString id, name, description, owner, repository, category;
    QStringList tags;
    bool enabled, featured;
    ReleaseChannel channel;             // nur Stable in V1
    QString assetPattern, executable, installDirectory;
};

class GitHubClient : public QObject {   // einziger Ort mit QNetworkAccessManager
    QNetworkReply* get(const QUrl& url, RequestKind kind);   // nur https, feste Host-Liste
    RateLimit rateLimit() const;        // aus X-RateLimit-Headern
};

class RegistryManager : public QObject {
    void refresh();                     // lädt, validiert, schreibt Cache
    QList<Project> projects() const;    // nur enabled
    RegistrySource source() const;      // Network | Cache | None
signals:
    void registryChanged();  void refreshFailed(RegistryError error);
};
```

Validierung als reine Funktion `RegistryParser::parse(QByteArray) -> ParseResult`, damit sie
ohne Netzwerk testbar ist. Erlaubte Hosts: `raw.githubusercontent.com`, `api.github.com`,
`github.com`, `objects.githubusercontent.com`, `release-assets.githubusercontent.com`.

### Phase 3 – Releases und Downloads

```cpp
struct SemVer { int major, minor, patch; QString preRelease;
    static std::optional<SemVer> parse(QStringView text);   // tolerant gegenüber Tag-Präfix
    std::strong_ordering operator<=>(const SemVer&) const; };

struct ReleaseAsset { QString name; QUrl downloadUrl; qint64 size; QByteArray sha256; };
struct Release { SemVer version; QString tag, notes; bool preRelease; QList<ReleaseAsset> assets; };

class ReleaseManager : public QObject {
    void fetchLatest(const Project& project);
signals:
    void releaseReady(const QString& projectId, const Release& release);
    void fetchFailed(const QString& projectId, ReleaseError error);
};

class DownloadManager : public QObject {    // Warteschlange, begrenzte Parallelität
    DownloadId enqueue(const DownloadRequest& request);
    void cancel(DownloadId id);
    QList<Download> downloads() const;
signals:
    void progress(DownloadId id, qint64 received, qint64 total, qint64 bytesPerSecond);
    void finished(DownloadId id);  void failed(DownloadId id, DownloadError error);
};
```

Downloads schreiben nach `temp/<id>.part`, prüfen die Größe gegen die Asset-Angabe und werden
erst nach bestandener Hash-Prüfung nach `downloads/` verschoben. Wiederholungen mit
Exponential-Backoff, höchstens drei Versuche, keine Wiederholung bei 4xx außer 429.

### Phase 4 – Installation

```cpp
namespace HashVerifier { VerifyResult verifySha256(const QString& file, const QByteArray& expected); }
namespace ArchiveValidator { ValidationResult validate(const QString& archive, const Limits& limits); }

class Installer : public QObject {      // Staging → Backup → Tausch → Registrierung
    void install(const Project& project, const Release& release, const QString& verifiedFile);
signals:
    void installed(const QString& projectId);  void failed(const QString& projectId, InstallError);
};
class Uninstaller { UninstallResult uninstall(const Project& project); };
```

Ablauf: nach `temp/staging-<id>/` entpacken → erwartete EXE prüfen → `current/` nach
`backups/<version>/` umbenennen → Staging nach `current/` umbenennen → `launcher.json`
schreiben. Jeder Fehler nach dem Backup stellt `current/` wieder her.

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
| `UiSmokeTests.cpp` | Navigation, Einstellungen über die UI | 1 |
| `RegistryTests.cpp`, `VersionTests.cpp`, `DownloadTests.cpp` | | 2–3 |
| `SecurityTests.cpp`, `InstallerTests.cpp` | | 4 |
| `UpdateTests.cpp`, Selbstupdate-Tests | | 5–6 |

## Stand

| Phase | Inhalt | Status |
|---|---|---|
| 1 | Grundgerüst | umgesetzt |
| 2 | Registry | offen; Entwicklung ohne Repository möglich, der echte Abruf braucht das Registry-Repository (E-03) |
| 3 | Releases und Downloads | offen |
| 4 | Installation | offen; der echte Test braucht ein Testprojekt mit ZIP-Releases (E-02) |
| 5 | Projekt-Updates | offen |
| 6 | Launcher-Selbstupdate | offen; braucht Releases in `KERSEX/KERJAMI_Launcher` |
| 7 | UI-Fertigstellung | offen |
| 8 | Portable Release | offen; braucht Entscheidung E-01 |

Offene Entscheidungen stehen in [03-entscheidungen.md](03-entscheidungen.md):

- E-01 Compiler und Qt-Variante (auch wegen der Qt-Lizenz, E-06)
- E-02 Testprojekt mit ZIP-Releases für den Test in Phase 4
- E-03 Registry-Repository (Name, Datei im Repository oder Release-Asset)
- E-04 ZIP-Bibliothek, E-05 Theme-Auswahl, E-06 Umfang der GitHub-Veröffentlichung

Die Reihenfolge der Phasen steht fest. Phase 2 und 3 laufen gegen einen lokalen Testserver und
hängen von keiner dieser Entscheidungen ab.
