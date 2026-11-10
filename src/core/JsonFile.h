#pragma once

#include <QJsonObject>
#include <QString>

/// Reading and writing of the launcher's JSON files.
///
/// Every configuration and metadata file goes through these functions so that
/// a crash or power loss while saving can never leave a half-written file.
namespace launcher::JsonFile {

enum class Status {
    Ok,
    NotFound,
    Corrupt,   ///< Not valid JSON, not an object, or implausibly large.
    ReadError, ///< The file exists but could not be opened.
};

struct LoadResult {
    Status status = Status::NotFound;
    QJsonObject object;
    QString error; ///< Technical detail for the log, empty on success.
};

enum class BackupMode {
    Move, ///< The original is renamed and no longer exists afterwards.
    Copy, ///< The original stays in place.
};

LoadResult load(const QString& path);

/// Writes to a temporary file in the same folder and replaces the target in one
/// step. On failure the previous file content is left untouched.
bool saveAtomic(const QString& path, const QJsonObject& object, QString* error = nullptr);

/// Preserves a file next to the original as "<name>.<tag>-<timestamp>.<ext>".
/// Returns the backup path, or an empty string if there was nothing to back up
/// or the operation failed.
QString backupAside(const QString& path, const QString& tag, BackupMode mode);

} // namespace launcher::JsonFile
