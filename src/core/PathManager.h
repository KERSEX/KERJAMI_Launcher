#pragma once

#include <QString>
#include <QStringList>

namespace launcher {

/// Resolves every location the launcher uses relative to one portable root.
///
/// The root is the folder that contains the executable. Nothing is stored
/// outside of it, so the whole folder can be moved to another drive. Paths that
/// are written to disk must be converted with toPortable() first.
class PathManager {
public:
    enum class PrepareError {
        None,
        RootMissing,           ///< The root folder does not exist.
        CannotCreateDirectory, ///< A data folder could not be created.
        NotWritable,           ///< A folder exists but files cannot be created in it.
    };

    struct PrepareResult {
        PrepareError error = PrepareError::None;
        QString path; ///< Folder the error refers to.

        bool ok() const { return error == PrepareError::None; }
    };

    /// Uses the folder of the running executable as root.
    static PathManager forApplication();

    explicit PathManager(const QString& rootDir);

    QString root() const { return m_root; }

    QString configDir() const;    ///< launcher/  - settings and launcher state
    QString libraryDir() const;   ///< library/   - installed projects
    QString downloadsDir() const; ///< downloads/ - verified downloads
    QString tempDir() const;      ///< temp/      - partial downloads and staging
    QString cacheDir() const;     ///< cache/     - registry and release cache
    QString logsDir() const;      ///< logs/

    QString settingsFile() const; ///< launcher/settings.json
    QString stateFile() const;    ///< launcher/launcher.json
    QString lockFile() const;     ///< launcher/launcher.lock

    QString registryCacheFile() const; ///< cache/registry.json - last valid registry

    /// All folders that hold user data and are created by prepare().
    QStringList dataDirectories() const;

    /// Creates the data folders and verifies that the root and each data folder
    /// are writable. Never falls back to another location.
    PrepareResult prepare() const;

    /// Converts an absolute path inside the root to a root-relative path with
    /// forward slashes. Returns an empty string for paths outside the root.
    QString toPortable(const QString& absolutePath) const;

    /// Inverse of toPortable(). Returns an empty string if the result would
    /// leave the root (absolute input, "..", drive letters).
    QString fromPortable(const QString& portablePath) const;

    /// True if @p path lies strictly inside @p baseDir.
    ///
    /// The comparison is lexical and case-insensitive, matching Windows file
    /// systems. Both paths are normalised first, so ".." segments cannot escape.
    /// Symbolic links and junctions are not resolved.
    static bool isInside(const QString& baseDir, const QString& path);

private:
    QString m_root;
};

} // namespace launcher
