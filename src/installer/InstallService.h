#pragma once

#include "downloads/DownloadManager.h"
#include "github/GitHubClient.h"
#include "github/ReleaseManager.h"
#include "installer/InstalledProjects.h"
#include "installer/Installer.h"
#include "installer/ProjectLauncher.h"
#include "installer/Uninstaller.h"
#include "models/Project.h"
#include "models/Release.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QThread;

namespace launcher {

/// What the launcher is doing with a project right now.
struct ProjectStatus {
    enum class Stage {
        Idle,
        FetchingRelease,  ///< Asking GitHub for the newest release.
        FetchingChecksum, ///< Reading the release's checksum file.
        Downloading,
        Verifying, ///< SHA-256 of the downloaded archive.
        Extracting,
        Replacing, ///< Moving the new version into place. Cannot be cancelled any more.
        Prepared,  ///< Downloaded and verified, waiting for installPrepared() (the project is running).
    };

    Stage stage = Stage::Idle;
    qint64 done = 0;
    qint64 total = 0; ///< 0 if unknown.
    QString version;  ///< The version that is being installed, once known.

    bool isBusy() const { return stage != Stage::Idle; }
};

/// Why the last operation on a project failed. The codes are the values of the
/// enum of the layer that failed, so the user interface can word each case.
struct OperationFailure {
    enum class Kind {
        None,
        Release,   ///< code: ReleaseManager::Problem
        NoHash,    ///< The release carries neither a GitHub digest nor a usable checksum file.
        Checksum,  ///< The checksum file could not be read or does not name the asset.
        Download,  ///< code: DownloadProblem
        Install,   ///< code: InstallError, subcode: ArchiveValidator::Problem
        Uninstall, ///< code: UninstallError
        Start,     ///< code: StartError
    };

    Kind kind = Kind::None;
    int code = 0;
    int subcode = 0;
    int httpStatus = 0;
    QString detail; ///< Technical detail for the log.

    bool isFailure() const { return kind != Kind::None; }
};

/// Installs, removes and starts projects: the steps the user takes in the library.
///
/// Installing runs the whole chain in order: newest release → the hash to check
/// against (GitHub's digest, otherwise the release's checksum file; without one
/// nothing is downloaded) → download → verification and installation by the
/// Installer. Each project has at most one operation at a time. The service owns
/// no state on disk; the list of installed projects stays with InstalledProjects.
class InstallService : public QObject {
    Q_OBJECT

public:
    InstallService(GitHubClient& client, ReleaseManager& releases, DownloadManager& downloads,
        Installer& installer, Uninstaller& uninstaller, InstalledProjects& installed,
        const LibraryLayout& layout, QObject* parent = nullptr);
    ~InstallService() override;

    /// How far install() goes.
    enum class Mode {
        InstallNow,  ///< All the way to the installed project.
        PrepareOnly, ///< Download and verify, then wait: prepared() is emitted, installPrepared() goes on.
    };

    /// Installs the newest stable release of the project, or replaces the
    /// installed version with it. Does nothing while an operation for the
    /// project is running.
    void install(const Project& project, Mode mode = Mode::InstallNow);

    /// Installs what install(…, PrepareOnly) has prepared. Does nothing for a
    /// project that is not in the Prepared stage.
    void installPrepared(const QString& projectId);

    /// Stops an operation that has not started to replace the installed version.
    /// A prepared update is discarded.
    void cancel(const QString& projectId);

    /// The user has confirmed. Fails with UninstallError::Busy during an installation.
    UninstallResult uninstall(const QString& projectId);

    /// How start() launches a project. The default starts it on its own
    /// (startProject); the application sets the ProcessManager's start here.
    void setStarter(std::function<StartResult(const QString& projectId)> starter)
    {
        m_starter = std::move(starter);
    }

    StartResult start(const QString& projectId);

    ProjectStatus status(const QString& projectId) const;
    bool isBusy(const QString& projectId) const { return m_sessions.contains(projectId); }
    int busyCount() const { return static_cast<int>(m_sessions.size()); }

    /// The last failure of an operation, until the next operation starts or dismissFailure().
    OperationFailure lastFailure(const QString& projectId) const { return m_failures.value(projectId); }
    void dismissFailure(const QString& projectId);

signals:
    /// Stage, progress or last failure of a project changed. Progress is reported at most about ten times a
    /// second.
    void statusChanged(const QString& projectId);

    /// An installation ended. On failure lastFailure() tells why; a cancelled installation ends with success
    /// = false and no failure.
    void installFinished(const QString& projectId, bool success);

    /// A PrepareOnly installation has downloaded and verified its archive and waits.
    void prepared(const QString& projectId);

private:
    struct Session {
        Project project;
        ProjectStatus status;
        Mode mode = Mode::InstallNow;
        std::optional<ReleaseSelection> selection;
        QString expectedSha256;
        DownloadId downloadId = 0;
        QString archivePath;
        QPointer<GitHubRequest> checksumRequest;
        std::shared_ptr<std::atomic<bool>> verifyCancel; ///< Set while the early verification runs.
        bool cancelled = false;
    };

    void startInstaller(Session& session);
    void startEarlyVerification(Session& session);
    void onEarlyVerified(const QString& projectId, int status, const QString& detail);
    void onReleaseReady(const QString& projectId, const ReleaseSelection& selection);
    void onReleaseFailed(const QString& projectId, ReleaseManager::Problem problem, int httpStatus);
    void fetchChecksum(Session& session);
    void onChecksum(const QString& projectId, const NetworkResult& result);
    void startDownload(Session& session);
    void onDownloadProgress(quint64 downloadId);
    void onDownloadStateChanged(quint64 downloadId);
    void onDownloadFinished(quint64 downloadId);
    void onDownloadFailed(quint64 downloadId);
    void onInstallerProgress(const QString& projectId, Installer::Stage stage, qint64 done, qint64 total);
    void onInstalled(const QString& projectId);
    void onInstallFailed(const QString& projectId, const InstallResult& result);

    Session* session(const QString& projectId);
    Session* sessionOfDownload(quint64 downloadId);
    void setStage(Session& session, ProjectStatus::Stage stage, qint64 done = 0, qint64 total = 0);
    void fail(const QString& projectId, const OperationFailure& failure);
    void finishCancelled(const QString& projectId);
    void finish(const QString& projectId, bool success);
    void removeArchive(Session& session);

    GitHubClient& m_client;
    ReleaseManager& m_releases;
    DownloadManager& m_downloads;
    Installer& m_installer;
    Uninstaller& m_uninstaller;
    InstalledProjects& m_installed;
    LibraryLayout m_layout;
    std::function<StartResult(const QString&)> m_starter;

    QHash<QString, Session> m_sessions;
    QHash<QString, OperationFailure> m_failures;
    QList<QThread*> m_verifiers;
};

} // namespace launcher
