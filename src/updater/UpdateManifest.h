#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace launcher {

/// One file of a launcher release.
struct ManifestFile {
    QString path;    ///< Relative to the launcher's folder, "/" separators.
    qint64 size = 0; ///< Bytes.
    QString sha256;  ///< Lowercase hex.

    bool operator==(const ManifestFile&) const = default;
};

/// The list of program files of a launcher release (update-manifest.json in the release ZIP).
///
/// Only the files listed here are ever written by an update, and only the
/// files in "remove" are ever deleted. Everything else in the launcher's
/// folder (settings, projects, downloads, logs, backups) is out of reach: the
/// parser refuses a manifest that names a path there.
struct UpdateManifest {
    int schemaVersion = 0;
    QString product;
    QString version;
    QString launcher; ///< Path of the launcher program, one of the files. It also does the update.
    QList<ManifestFile> files;
    QStringList remove; ///< Files of an earlier release that no longer belong to the program.

    const ManifestFile* find(const QString& path) const;
    bool operator==(const UpdateManifest&) const = default;
};

/// What a manifest must say about itself to be accepted.
struct ManifestExpectations {
    QString product;      ///< Product name; a package of another product is refused.
    QString launcherFile; ///< File name of the launcher program, e.g. "KERJAMI_Launcher.exe".
};

struct ManifestParseResult {
    bool ok = false;
    QString error; ///< Why the manifest was refused, for the log and the user.
    UpdateManifest manifest;
};

namespace UpdateManifestFormat {

inline constexpr int kSchemaVersion = 1;
inline constexpr qsizetype kMaxFiles = 200;
inline constexpr qsizetype kMaxRemove = 200;
inline constexpr qint64 kMaxFileSize = 2LL * 1024 * 1024 * 1024;

/// Name of the manifest inside the release ZIP. It is never installed itself.
inline const QString kFileName = QStringLiteral("update-manifest.json");

/// Reads and checks a manifest. The text comes from a downloaded archive and
/// is treated as untrusted input: every path must be a plain relative file
/// path of the kind Windows can store, must not lie in a folder the launcher
/// keeps for the user's data, and must not appear twice (also not when only the
/// case differs). The launcher program must be listed. Keys this version does not
/// know (an "updater" from a draft of the format) are ignored.
ManifestParseResult parse(const QByteArray& json, const ManifestExpectations& expectations);

/// A manifest as JSON text, for tests and for the packaging script's reference.
QByteArray toJson(const UpdateManifest& manifest);

/// True if an update must never touch the path: the folders of the user's data
/// (launcher, library, downloads, temp, cache, logs, updater) at any depth of
/// their own tree, and the manifest file itself. Case is ignored.
bool isProtectedPath(const QString& relativePath);

/// The path as it must be written in a manifest, or an empty string if it is
/// not acceptable. @p reason says why.
QString checkedPath(const QString& path, QString* reason);

} // namespace UpdateManifestFormat

} // namespace launcher
