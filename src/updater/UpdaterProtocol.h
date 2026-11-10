#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace launcher {

/// What the launcher tells the updater program (a JSON file next to its copy).
///
/// The updater trusts none of it more than it must: it checks that every path
/// lies inside the launcher's folder and validates the manifest itself.
struct UpdaterJob {
    static constexpr int kSchemaVersion = 1;
    static constexpr int kDefaultWaitTimeoutMs = 60000;

    QString rootDir;    ///< Absolute path of the launcher's folder.
    QString stagingDir; ///< Absolute; the extracted release.
    QString fromVersion;
    QString toVersion;
    QString launcherFile;         ///< File name of the launcher program, to start again afterwards.
    QString updaterFile;          ///< File name of the updater program, as the manifest names it.
    QString product;              ///< Product name; the manifest must be for it.
    qint64 launcherProcessId = 0; ///< The updater waits until this process has ended.
    int waitTimeoutMs = kDefaultWaitTimeoutMs;
    bool startLauncherAfter = true;

    QJsonObject toJson() const;
    static std::optional<UpdaterJob> fromJson(const QJsonObject& object);
};

/// How an update ended. Written by the updater, read once by the launcher.
struct UpdateReport {
    enum class Status {
        Success,    ///< The new version is installed.
        RolledBack, ///< The update failed; the old version is back.
        NotApplied, ///< Nothing was changed (refused before, or the launcher did not close).
        Failed,     ///< The update failed and the old version could not be restored completely.
    };

    Status status = Status::NotApplied;
    QString fromVersion;
    QString toVersion;
    QString detail;               ///< Technical detail.
    QString failedPath;           ///< The path that stopped the update, if any.
    QString backupDir;            ///< Relative to the launcher's folder: where the replaced files are.
    bool launcherStarted = false; ///< The updater started the launcher again.
    QDateTime finishedAt;

    QJsonObject toJson() const;
    static std::optional<UpdateReport> fromJson(const QJsonObject& object);
};

QString statusToString(UpdateReport::Status status);

} // namespace launcher
