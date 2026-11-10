#pragma once

#include <QString>

namespace launcher {

struct InstalledProject;

/// Where a project's files live, derived from the portable folder.
///
/// library/<install directory>/current/     the installed version
/// library/<install directory>/backups/     previous versions, one folder each
/// temp/staging-<project id>/               extraction in progress
/// temp/uninstall-<project id>/             removed project, waiting to be deleted
///
/// Staging and the library are on the same drive (both are inside the portable
/// folder), so moving a finished extraction into place is a rename: it either
/// happens completely or not at all.
class LibraryLayout {
public:
    LibraryLayout(const QString& libraryDir, const QString& tempDir);

    QString libraryDir() const { return m_libraryDir; }
    QString tempDir() const { return m_tempDir; }

    /// The arguments must have passed RegistryParser::isValidInstallDirectory and
    /// isValidProjectId; callers check before they build paths.
    QString projectDir(const QString& installDirectory) const;
    QString currentDir(const QString& installDirectory) const;
    QString backupsDir(const QString& installDirectory) const;
    QString stagingDir(const QString& projectId) const;
    QString trashDir(const QString& projectId) const;

    /// The file that starts an installed project.
    QString executablePath(const InstalledProject& project) const;

    /// Deletes what an interrupted installation or removal left in temp/:
    /// staging and trash folders. Only call while no installer is running.
    /// Returns the number of folders removed.
    int cleanupTemp() const;

    static constexpr auto kStagingPrefix = "staging-";
    static constexpr auto kTrashPrefix = "uninstall-";

private:
    QString m_libraryDir;
    QString m_tempDir;
};

} // namespace launcher
