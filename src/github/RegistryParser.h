#pragma once

#include "models/Project.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace launcher {

/// An entry that was rejected, with the reason for the log.
struct RegistryIssue {
    int index = -1;    ///< Position in the "projects" array.
    QString projectId; ///< The entry's id if it had a usable one.
    QString message;
};

struct RegistryParseResult {
    enum class Status {
        Ok,
        InvalidJson,        ///< Not JSON.
        InvalidStructure,   ///< JSON, but not a registry document.
        UnsupportedVersion, ///< A schema_version this launcher does not know.
    };

    Status status = Status::InvalidJson;
    QString error; ///< Technical detail when status is not Ok.

    int schemaVersion = 0;
    QString registryName;

    /// Valid and enabled projects in file order. Entries that are disabled or
    /// were rejected are not in this list.
    QList<Project> projects;
    int disabledCount = 0;
    QList<RegistryIssue> issues;

    /// The parsed document, kept for the cache. Filled once the JSON itself was readable.
    QJsonObject document;

    bool ok() const { return status == Status::Ok; }
};

/// Reads and validates the project registry (projects.json).
///
/// The registry is the only source of projects, so everything in it is treated
/// as untrusted input. A document with a broken structure is rejected as a
/// whole. Inside a valid document each entry stands on its own: an invalid entry
/// is dropped and reported, the others stay. Entries that clash (same id or
/// same install directory) are all dropped, because the registry gives no way
/// to tell which one is meant.
namespace RegistryParser {

inline constexpr int kSupportedSchemaVersion = 1;
inline constexpr qsizetype kMaxProjects = 500;

RegistryParseResult parse(const QByteArray& json);
RegistryParseResult parse(const QJsonObject& root);

// The rules for single fields, also used by the installer to check a project
// again before it touches the file system.

/// Lowercase letters, digits and single hyphens, at most 48 characters.
bool isValidProjectId(const QString& id);

/// Ids the registry may not give to a project because the launcher uses them
/// for itself: its own updates are fetched and stored like a project named
/// "launcher". Such an id passes isValidProjectId but never comes out of the parser.
bool isReservedProjectId(const QString& id);

/// The id under which the launcher's own releases and downloads are handled.
inline const QString kLauncherProjectId = QStringLiteral("launcher");

/// A GitHub account name.
bool isValidOwner(const QString& owner);

/// A GitHub repository name; "." and ".." and a ".git" suffix are refused.
bool isValidRepository(const QString& repository);

/// A single folder name, safe on Windows: no path, no reserved device name, no trailing dot.
bool isValidInstallDirectory(const QString& directory);

/// A ZIP file name; "*" is the only wildcard.
bool isValidAssetPattern(const QString& pattern);

/// The text a release tag starts with before the version: 1 to 32 letters, digits,
/// ".", "_" or "-", beginning with a letter. No "/", so that a tag stays one path
/// segment of a download address.
bool isValidTagPrefix(const QString& prefix);

/// A relative path with "/" separators that ends in ".exe" and cannot leave the install folder.
bool isValidExecutablePath(const QString& path);

/// True for names Windows reserves for devices ("CON", "NUL", "COM1", ...),
/// also with an extension ("con.txt"). Such a file or folder cannot be created.
bool isReservedWindowsName(const QString& name);

} // namespace RegistryParser

} // namespace launcher
