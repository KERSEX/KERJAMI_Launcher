#pragma once

#include <QString>

namespace launcher::FileOps {

/// True for symbolic links and, on Windows, junctions. Both can point anywhere.
bool isLink(const QString& path);

/// Deletes a folder with everything in it.
///
/// Links are removed, never followed: a link inside the tree that points to
/// another folder removes the link only. Read-only files are made writable
/// first. Returns true if the folder is gone afterwards; @p error then is empty.
/// On failure, as much as possible has been deleted and @p error names the
/// first path that resisted.
bool removeTree(const QString& path, QString* error = nullptr);

/// Copies a file, or a folder with everything in it, to @p to. A folder is merged into one that is
/// already there, and a file replaces one of the same name; an existing item of the other kind (a
/// file where a folder is wanted, or the reverse) is removed first. Links are neither followed nor
/// copied: a link inside the tree is left out. On failure @p error names the first path that failed;
/// what was copied until then stays.
bool copyTree(const QString& from, const QString& to, QString* error = nullptr);

/// Renames a folder or file. A rename can fail for a moment on Windows because
/// a virus scanner or the search indexer holds a file; it is tried a few times.
bool renameWithRetry(const QString& from, const QString& to, QString* error = nullptr);

} // namespace launcher::FileOps
