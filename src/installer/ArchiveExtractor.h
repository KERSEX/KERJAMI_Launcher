#pragma once

#include "security/ArchiveValidator.h"
#include "security/ZipArchive.h"

#include <QString>

#include <atomic>
#include <functional>

namespace launcher::ArchiveExtractor {

enum class Status {
    Ok,
    Cancelled,
    UnsafeTarget, ///< A target would lie outside the folder, or something is in the way.
    FileError,    ///< A folder or file could not be created or written.
    DataError,    ///< The archive data is damaged or contradicts its directory.
};

struct Result {
    Status status = Status::Ok;
    QString detail; ///< Technical detail for the log; names the entry.

    bool ok() const { return status == Status::Ok; }
};

/// Bytes written so far and the total of all files.
using Progress = std::function<void(qint64 done, qint64 total)>;

/// Writes the entries a validation approved into an empty folder.
///
/// Only the items of @p validation are written, each to a path that was
/// checked against @p targetDir once more here. A file is created with
/// "must not exist", so an entry can never overwrite another file. On failure
/// the folder is left as it is; the caller removes it.
Result extract(ZipArchive& archive, const ArchiveValidator::ValidationResult& validation,
    const QString& targetDir, const std::atomic<bool>* cancel = nullptr, const Progress& progress = {});

} // namespace launcher::ArchiveExtractor
