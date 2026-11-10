#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>

#include <optional>

namespace launcher {

/// What the launcher knows about one installed project.
struct InstalledProject {
    QString projectId;
    QString version;          ///< Semantic version of the installed release.
    QString tag;              ///< Release tag, for display.
    QString installDirectory; ///< Folder name below library/. Never a path.
    QString executable;       ///< Start file below current/, "/" separators, checked at install time.
    QDateTime installedAt;
    QDateTime lastStartedAt; ///< Invalid until the project was started from the launcher.

    bool operator==(const InstalledProject&) const = default;
};

/// The list of installed projects (launcher/launcher.json).
///
/// Installing and removing a project changes this list; nothing else does.
/// Every change is written at once and atomically. Records hold names, never
/// absolute paths, so the folder can be moved with the launcher.
///
/// The files on disk are the truth and the list is the launcher's memory of
/// them. A project whose folder was deleted by hand is reported by the
/// installer as "not found" when it is started; the list itself is not
/// reconciled with the disk.
///
/// All functions may be called from any thread: the installer registers a
/// finished installation from its worker thread. The changed() signal is
/// delivered to listeners in their own thread.
class InstalledProjects : public QObject {
    Q_OBJECT

public:
    static constexpr int kSchemaVersion = 1;

    enum class LoadResult {
        Loaded,
        CreatedEmpty,             ///< No file yet.
        RecoveredFromCorruption,  ///< Unreadable file moved aside; the list starts empty.
        RecoveredFromNewerSchema, ///< File of a newer launcher moved aside; the list starts empty.
    };

    explicit InstalledProjects(const QString& filePath, QObject* parent = nullptr);

    /// Reads the file. Records that do not pass the checks are dropped and logged.
    LoadResult load();

    QString filePath() const { return m_filePath; }

    /// Where load() preserved an unusable file, empty if it did not have to.
    QString backupPath() const { return m_backupPath; }

    bool contains(const QString& projectId) const;
    std::optional<InstalledProject> get(const QString& projectId) const;
    QList<InstalledProject> all() const;

    /// Adds or replaces a record and saves. If saving fails the list is left as it was.
    bool upsert(const InstalledProject& project, QString* error = nullptr);

    /// Removes a record and saves. Returns true if there was none. If saving
    /// fails the list is left as it was.
    bool remove(const QString& projectId, QString* error = nullptr);

    /// Notes the time a project was started. Failing to save is only logged.
    void recordStart(const QString& projectId, const QDateTime& when);

signals:
    /// A record was added, replaced or removed.
    void changed(const QString& projectId);

private:
    bool saveLocked(QString* error);

    QString m_filePath;
    QString m_backupPath;
    mutable QMutex m_mutex; ///< Guards m_projects, m_foreign and the file.
    QHash<QString, InstalledProject> m_projects;

    /// Everything in the file that is not the project list, written back unchanged.
    QJsonObject m_foreign;
};

} // namespace launcher
