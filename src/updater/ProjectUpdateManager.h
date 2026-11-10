#pragma once

#include "core/SettingsManager.h"
#include "github/RegistryManager.h"
#include "github/ReleaseManager.h"
#include "installer/InstallService.h"
#include "installer/InstalledProjects.h"
#include "process/ProcessManager.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

namespace launcher {

/// Where a project stands with respect to its updates.
enum class UpdateState {
    Unknown, ///< Not checked yet in this session.
    UpToDate,
    Available,   ///< A newer version exists. In manual mode the user decides.
    Queued,      ///< Waiting for a free place in the update queue.
    Downloading, ///< Asking GitHub, downloading.
    Verifying,   ///< SHA-256 of the archive.
    Pending,     ///< Downloaded and verified, waiting for the project to be closed. ("Update Pending")
    Installing,
    Failed, ///< The last attempt failed; the installed version is as it was. See UpdateInfo::failure.
};

struct UpdateInfo {
    UpdateState state = UpdateState::Unknown;

    QString installedVersion;
    QString latestVersion; ///< The newest stable version GitHub reported; empty before the first check.
    QString tag;
    QString notes; ///< Release notes of the latest version, the changelog.
    QDateTime publishedAt;
    qint64 assetSize = 0;

    QDateTime checkedAt;          ///< Last successful check.
    bool lastCheckFailed = false; ///< The last check could not reach GitHub; the rest is from an earlier one.

    bool automatic = false;   ///< The running update was started by automatic mode.
    OperationFailure failure; ///< Why the last attempt failed.

    /// A version newer than the installed one is known.
    bool updateKnown() const { return state != UpdateState::Unknown && state != UpdateState::UpToDate; }
};

/// Looks for new versions of installed projects and, depending on each
/// project's mode, announces them or installs them.
///
/// Only installed projects are ever looked at, and a project is only ever
/// replaced by a newer version of itself: nothing is installed because a
/// project shows up in the registry.
///
/// Manual mode: the new version is announced (state Available) and the user
/// starts the update. Automatic mode: the update is downloaded and verified in
/// the background and installed as soon as the project is not running. In both
/// modes a running project is never touched: the update waits (Pending) and
/// goes on when the project has ended.
///
/// A failed update leaves the installed version as it was. Automatic mode does
/// not try the same version again by itself, so that a broken release cannot
/// cause a download every hour; the user can retry (applyUpdate), and the next
/// version starts over. A network problem during a check never creates or
/// removes an update: the last known state stays.
class ProjectUpdateManager : public QObject {
    Q_OBJECT

public:
    /// Updates that download and install at the same time. Further ones wait in the queue.
    static constexpr int kMaxParallelUpdates = 2;

    /// A project is not asked about again within this time, however often checkAll() is called.
    static constexpr int kMinSecondsBetweenChecks = 60;

    /// GitHub allows 60 requests per hour without login. Checks stop when fewer than
    /// this many plus one per project are left, so that installing still works.
    static constexpr int kRateLimitReserve = 10;

    ProjectUpdateManager(GitHubClient& client, RegistryManager& registry, ReleaseManager& releases,
        InstallService& installService, ProcessManager& processes, InstalledProjects& installed,
        SettingsManager& settings, QObject* parent = nullptr);

    /// How long a project is left alone after it was asked about. Only tests change this.
    void setMinCheckSpacingSeconds(int seconds) { m_minCheckSpacing = seconds; }

    /// Starts the periodic check (interval from the settings) and one check a few seconds from now.
    void start();

    /// Asks GitHub about every installed project, one after the other.
    void checkAll();

    UpdateInfo info(const QString& projectId) const;

    /// The user wants this update now (after confirming, in manual mode) or tries again after a failure.
    /// Does nothing unless a newer version is known.
    void applyUpdate(const QString& projectId);

    /// Stops a queued, running or pending update; the version stays announced.
    void cancel(const QString& projectId);

    /// Projects waiting for a place in the queue, in the order they will start.
    QStringList queue() const { return m_queue; }

    /// Projects whose update is downloading, verifying, pending or installing.
    QStringList active() const { return m_active.values(); }

signals:
    void updateChanged(const QString& projectId);

    /// A round of checks ended (also if it was skipped).
    void checkFinished();

private:
    void applyIntervalSetting();
    void nextCheck();
    void onReleaseReady(const QString& projectId, const ReleaseSelection& selection);
    void onReleaseFailed(const QString& projectId, ReleaseManager::Problem problem);
    void evaluate(const QString& projectId, const ReleaseSelection& selection);

    void enqueue(const QString& projectId, bool automatic);
    void pump();
    void onServiceStatus(const QString& projectId);
    void onPrepared(const QString& projectId);
    void onInstallFinished(const QString& projectId, bool success);
    void tryInstallPending(const QString& projectId);
    void onRunningChanged(const QString& projectId, bool running);
    void onInstalledChanged(const QString& projectId);
    void onProjectSettingsChanged(const QString& projectId);

    std::optional<Project> registryProject(const QString& projectId) const;
    void setState(const QString& projectId, UpdateState state);
    void refreshInstalledVersion(const QString& projectId);

    GitHubClient& m_client;
    RegistryManager& m_registry;
    ReleaseManager& m_releases;
    InstallService& m_service;
    ProcessManager& m_processes;
    InstalledProjects& m_installed;
    SettingsManager& m_settings;

    QTimer m_timer;
    int m_minCheckSpacing = kMinSecondsBetweenChecks;
    QHash<QString, UpdateInfo> m_info;

    QStringList m_checkQueue;
    QString m_checking;
    QHash<QString, QDateTime> m_lastAsked;

    QStringList m_queue;
    QSet<QString> m_active;
    QSet<QString> m_installingNow; ///< Updates whose installation has started (the archive is out of reach).
    /// A version that automatic mode does not start again by itself (failed or cancelled).
    QHash<QString, QString> m_noAutoVersion;
};

} // namespace launcher
