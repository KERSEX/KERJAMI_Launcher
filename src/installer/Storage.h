#pragma once

#include <QString>

#include <atomic>

namespace launcher {

class PathManager;

/// How much room the launcher's folders take, and the cleaning up the user can ask for
/// (docs/03-entscheidungen.md, D-45).
///
/// Everything here stays inside the portable folder and never follows a link.
namespace Storage {

struct Usage {
    qint64 downloadsAndTemp = 0; ///< downloads/ and temp/: files of an unfinished or abandoned transfer
    qint64 backups = 0;          ///< library/<project>/backups/: the earlier versions of projects
};

/// Walks the folders; with big projects this takes a moment, so call it from a worker thread.
/// Stops early (with what it has counted) when @p cancel becomes true.
Usage measure(const PathManager& paths, const std::atomic<bool>* cancel = nullptr);

/// Deletes everything in downloads/ and temp/ (the folders stay). Call it only while no
/// download, installation or update is running: a transfer's partial file lives there.
/// False if something could not be deleted; the rest is deleted anyway.
bool clearDownloadsAndTemp(const PathManager& paths, QString* error = nullptr);

/// Deletes the saved earlier versions of every project, library/<project>/backups/*.
/// The installed versions (current/) are not touched. Same condition and result as above.
bool clearBackups(const PathManager& paths, QString* error = nullptr);

} // namespace Storage

} // namespace launcher
