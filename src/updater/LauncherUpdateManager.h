#pragma once

#include "core/PathManager.h"
#include "core/SettingsManager.h"
#include "downloads/DownloadManager.h"
#include "github/GitHubClient.h"
#include "github/ReleaseManager.h"
#include "updater/UpdateManifest.h"
#include "updater/UpdatePackage.h"
#include "updater/UpdaterProtocol.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QThread;

namespace launcher {

enum class LauncherUpdateState {
    Idle, ///< Not checked yet in this session.
    Checking,
    UpToDate,
    Available,   ///< A newer version exists; the user decides.
    Downloading, ///< Release, checksum, download.
    Preparing,   ///< SHA-256, extraction, manifest, every file.
    Ready,       ///< Staged and checked; waits for the restart.
    Applying,    ///< The updater has been started; the launcher is about to close.
    Failed,      ///< The last step failed; see LauncherUpdateInfo::failure.
};

/// Why a step of the launcher's own update failed.
struct LauncherUpdateFailure {
    enum class Kind {
        None,
        Release,  ///< code: ReleaseManager::Problem
        NoHash,   ///< The release carries neither a GitHub digest nor a usable checksum file.
        Checksum, ///< The checksum file could not be read or does not name the asset.
        Download, ///< code: DownloadProblem
        Package,  ///< code: PackageError, subcode: ArchiveValidator::Problem
        Busy,     ///< Projects are being installed; the update waits for them.
        Handoff,  ///< The updater could not be started, or the launcher is not set up for updating.
    };

    Kind kind = Kind::None;
    int code = 0;
    int subcode = 0;
    int httpStatus = 0;
    QString detail; ///< Technical detail for the log.

    bool isFailure() const { return kind != Kind::None; }
};

struct LauncherUpdateInfo {
    LauncherUpdateState state = LauncherUpdateState::Idle;
    QString currentVersion;
    QString latestVersion;
    QString tag;
    QString notes; ///< The release notes, the changelog.
    QDateTime publishedAt;
    qint64 assetSize = 0;
    QDateTime checkedAt;
    bool lastCheckFailed = false; ///< The last check could not reach GitHub; the rest is from an earlier one.
    qint64 done = 0;              ///< Progress of Downloading and Preparing.
    qint64 total = 0;
    LauncherUpdateFailure failure;

    bool updateKnown() const
    {
        return state == LauncherUpdateState::Available || state == LauncherUpdateState::Downloading
            || state == LauncherUpdateState::Preparing || state == LauncherUpdateState::Ready
            || state == LauncherUpdateState::Applying
            || (state == LauncherUpdateState::Failed && !latestVersion.isEmpty());
    }
};

/// The launcher's own update: look for a newer release, download and check it,
/// and hand it to LauncherUpdater.exe, which replaces the program files after
/// the launcher has closed.
///
/// The launcher never replaces its own running file. Nothing happens without
/// the user: a new version is announced, and only a confirmed update is
/// downloaded. The checks are those of project releases (an announced SHA-256,
/// a safe archive) plus the release's manifest, which lists the program files
/// and their hashes. See docs/03-entscheidungen.md, D-39 and D-40.
class LauncherUpdateManager : public QObject {
    Q_OBJECT

public:
    /// How the updater program is started: its path and the job file. True if it was started.
    using UpdaterStarter = std::function<bool(const QString& updaterProgram, const QString& jobFile)>;

    LauncherUpdateManager(GitHubClient& client, ReleaseManager& releases, DownloadManager& downloads,
        SettingsManager& settings, const PathManager& paths, QObject* parent = nullptr);
    ~LauncherUpdateManager() override;

    /// True while something else must not be interrupted by a restart: a project is being installed.
    void setBusyCheck(std::function<bool()> busy) { m_isBusy = std::move(busy); }

    /// Starts the updater program. The default starts it detached from the launcher.
    void setUpdaterStarter(UpdaterStarter starter) { m_startUpdater = std::move(starter); }

    /// Only tests change these: the version of the running launcher, the name of
    /// its program file, and the product the manifest must be for.
    void setCurrentVersion(const QString& version)
    {
        m_currentVersion = version;
        m_info.currentVersion = version;
    }
    void setRunningFileName(const QString& fileName) { m_runningFile = fileName; }
    void setExpectations(const ManifestExpectations& expectations) { m_expectations = expectations; }
    void setProcessId(qint64 id) { m_processId = id; }

    /// Looks for a newer release now, and then in the interval of the settings
    /// while the setting "Launcher-Updates suchen" is on. The first look comes a few seconds later.
    void start();

    /// Looks for a newer release now, whatever the setting says.
    void checkNow();

    LauncherUpdateInfo info() const;

    /// The user confirmed. Downloads and checks the newest release and, once it is
    /// ready, restarts into it. Does nothing unless a newer version is announced.
    void install();

    /// Stops a download or a preparation; a staged update is discarded.
    void cancel();

    /// Hands the staged update to the updater and asks the application to close.
    /// Does nothing unless the update is ready. Refused while projects are being installed.
    void apply();

signals:
    void infoChanged();

    /// The updater has been started; the application must close now.
    void restartRequested();

private:
    struct Pending {
        ReleaseSelection selection;
        QString expectedSha256;
        DownloadId downloadId = 0;
        QString archivePath;
        QString stagingDir;
        UpdateManifest manifest;
        std::shared_ptr<std::atomic<bool>> cancel;
        bool restartWhenReady = false;
    };

    Project launcherProject() const;
    void onReleaseReady(const QString& projectId, const ReleaseSelection& selection);
    void onReleaseFailed(const QString& projectId, ReleaseManager::Problem problem, int httpStatus);
    void fetchChecksum();
    void onChecksum(const NetworkResult& result);
    void startDownload();
    void onDownloadProgress(quint64 id);
    void onDownloadStateChanged(quint64 id);
    void onDownloadFinished(quint64 id);
    void onDownloadFailed(quint64 id);
    void startPreparation();
    void onPrepared(const PackageResult& result);
    void fail(const LauncherUpdateFailure& failure);
    void discardStaging();
    void removeArchive();
    void setState(LauncherUpdateState state);
    bool busy() const { return m_isBusy && m_isBusy(); }

    GitHubClient& m_client;
    ReleaseManager& m_releases;
    DownloadManager& m_downloads;
    SettingsManager& m_settings;
    PathManager m_paths;

    std::function<bool()> m_isBusy;
    UpdaterStarter m_startUpdater;
    QString m_currentVersion;
    QString m_runningFile;
    ManifestExpectations m_expectations;
    qint64 m_processId = 0;

    QTimer m_timer;
    LauncherUpdateInfo m_info;
    LauncherUpdateState m_stateBeforeCheck = LauncherUpdateState::Idle;
    bool m_checking = false;
    std::optional<ReleaseSelection> m_latest; ///< The newest release found by the last successful check.
    std::optional<Pending> m_pending;
    QPointer<GitHubRequest> m_checksumRequest;
    QList<QThread*> m_threads;
};

} // namespace launcher

Q_DECLARE_METATYPE(launcher::PackageResult)
