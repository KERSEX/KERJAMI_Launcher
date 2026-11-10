#pragma once

#include "security/ZipArchive.h"

#include <QList>
#include <QString>

namespace launcher::ArchiveValidator {

/// What an archive may contain. The defaults suit a desktop application of a
/// few hundred megabytes; the installer narrows the path length to the
/// location it extracts to.
struct Limits {
    int maxEntries = 20000;
    qint64 maxEntrySize = 2LL * 1024 * 1024 * 1024;
    qint64 maxTotalSize = 4LL * 1024 * 1024 * 1024; ///< Sum of all uncompressed files.

    int maxPathLength = 200; ///< Characters of the path inside the archive.
    int maxDepth = 24;       ///< Folders above a file.

    /// Uncompressed size divided by compressed size. Deflate cannot exceed
    /// about 1030, and real programs stay far below; only entries of at least
    /// kRatioFloor bytes are measured, since tiny files compress arbitrarily
    /// well. The size limits above are the real protection against a bomb.
    int maxCompressionRatio = 500;
    static constexpr qint64 kRatioFloor = 4LL * 1024 * 1024;
};

enum class Problem {
    None,
    Empty, ///< The archive has no files.
    TooManyEntries,
    TooLarge,          ///< A file or the total is larger than the limits allow.
    UnsafePath,        ///< Absolute, leading outside, reserved name, forbidden character, too long.
    DuplicatePath,     ///< Two entries end up at the same place on a case-insensitive file system.
    ConflictingPath,   ///< A name is used as a file and as a folder.
    UnsupportedEntry,  ///< Symbolic link or special file.
    CompressionBomb,   ///< Far more data than the compressed size suggests.
    MissingExecutable, ///< The file the project starts is not in the archive.
};

/// One entry that passed all checks, with the path where it may be written.
struct Item {
    QString path; ///< Normalised relative path, "/" separators, no trailing slash.
    bool isDirectory = false;
    qint64 size = 0;
    int entryIndex = -1; ///< Index into ZipArchive::entries().
};

struct ValidationResult {
    Problem problem = Problem::None;
    QString detail; ///< Names the offending entry, for the log and the error message.

    QList<Item> items;    ///< In archive order.
    qint64 totalSize = 0; ///< Sum of the uncompressed file sizes.

    bool ok() const { return problem == Problem::None; }
};

/// Checks every entry before anything is written.
///
/// Nothing in the archive is trusted: names are normalised and then judged by
/// the rules of Windows (a path that is harmless on Linux can still escape or
/// collide here), sizes are summed, and the result lists exactly what the
/// extractor may create. An archive that fails any rule is rejected as a
/// whole; the launcher never extracts "the good part".
///
/// @p requiredExecutable is the project's start file. It must be a file in the
/// archive; pass an empty string to skip the check.
ValidationResult validate(const ZipArchive& archive, const Limits& limits, const QString& requiredExecutable);

/// The rules for a single name, also usable on its own. Returns the normalised
/// path (backslashes become "/", one trailing slash of a folder is removed) or
/// an empty string if the name must be refused; @p reason then says why.
QString normalizeEntryPath(const QString& name, const Limits& limits, bool* isDirectory, QString* reason);

} // namespace launcher::ArchiveValidator
