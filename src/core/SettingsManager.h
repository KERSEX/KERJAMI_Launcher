#pragma once

#include "models/LauncherSettings.h"
#include "models/ProjectSettings.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace launcher {

/// Loads and stores the user's launcher preferences (launcher/settings.json).
///
/// Every change is written to disk immediately and atomically. A damaged file
/// never blocks the start: it is preserved under a new name and replaced by
/// defaults.
class SettingsManager : public QObject {
    Q_OBJECT

public:
    /// Version of the file layout written by this build.
    static constexpr int kSchemaVersion = 1;

    enum class LoadResult {
        Loaded,                   ///< Existing file read as it is.
        CreatedDefaults,          ///< No file yet; defaults were written.
        Migrated,                 ///< Older layout upgraded; a copy of the old file was kept.
        RecoveredFromCorruption,  ///< Unreadable file moved aside; defaults were written.
        RecoveredFromNewerSchema, ///< File of a newer launcher moved aside; defaults were written.
    };

    explicit SettingsManager(const QString& filePath, QObject* parent = nullptr);

    /// Reads the file. Always leaves the manager with usable settings.
    LoadResult load();

    const LauncherSettings& settings() const { return m_settings; }
    QString filePath() const { return m_filePath; }

    /// Where load() preserved the previous file, empty if it did not have to.
    QString backupPath() const { return m_backupPath; }

    // Each setter persists immediately and returns false if writing failed.
    // The new value stays active in memory either way.
    bool setTheme(ThemeId theme);
    bool setStartPage(StartPage page);
    bool setLauncherUpdateAutoCheck(bool enabled);

    /// The value is clamped to the supported range.
    bool setUpdateCheckIntervalMinutes(int minutes);

    // Settings of single projects, stored by project id in the section "projects".
    // They do not depend on the installed version or on the registry: a project
    // that disappears from the registry and comes back keeps its choice.

    /// Manual for every project that has no entry. Only the user's choice can make it automatic.
    UpdateMode updateMode(const QString& projectId) const;

    /// Persists at once. Returns false for an id that is not a valid project id,
    /// or if saving failed (the value stays active in memory then).
    bool setUpdateMode(const QString& projectId, UpdateMode mode);

    /// Forgets a project's settings, for example after it was uninstalled.
    void removeProjectSettings(const QString& projectId);

signals:
    void settingsChanged();

    /// The settings of one project changed or were removed.
    void projectSettingsChanged(const QString& projectId);

    /// The settings file could not be written. @p reason is a technical detail.
    void saveFailed(const QString& reason);

private:
    template <typename T> bool assign(T LauncherSettings::* member, T value);
    bool save();

    QString m_filePath;
    QString m_backupPath;
    LauncherSettings m_settings;
    QHash<QString, ProjectSettings> m_projects;

    /// Last file content including keys this build does not know. Saving
    /// merges into it, so foreign sections are never dropped.
    QJsonObject m_raw;
};

} // namespace launcher
