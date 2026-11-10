#pragma once

#include "security/ArchiveValidator.h"
#include "updater/UpdateManifest.h"

#include <QString>

#include <atomic>
#include <functional>

namespace launcher {

struct PackageRequest {
    QString archivePath;     ///< The downloaded release ZIP.
    QString expectedSha256;  ///< Required: without a hash nothing is prepared.
    QString stagingDir;      ///< Absolute. Emptied, then the release is extracted here.
    QString rootDir;         ///< The launcher's folder; its path length limits the files' paths.
    QString expectedVersion; ///< The version of the release; the manifest must say the same.
    ManifestExpectations expectations;
    ArchiveValidator::Limits limits;
    /// Free space of the folder's drive in bytes, -1 if unknown. Replaced by tests.
    std::function<qint64(const QString&)> availableSpace;
};

enum class PackageError {
    None,
    NoTrustedHash,
    HashMismatch,
    ArchiveUnreadable,
    ArchiveRejected, ///< A safety rule of the archive validator, see PackageResult::validationProblem.
    PathTooLong,
    InsufficientSpace,
    ExtractionFailed,
    ManifestMissing,
    ManifestInvalid, ///< Not readable, or breaks a rule (path, product, schema, ...).
    VersionMismatch, ///< The manifest is for another version than the release.
    FilesMismatch,   ///< A listed file is missing or differs in size or hash.
    Cancelled,
};

struct PackageResult {
    PackageError error = PackageError::None;
    QString detail; ///< Technical detail for the log.
    ArchiveValidator::Problem validationProblem = ArchiveValidator::Problem::None;
    UpdateManifest manifest;

    bool ok() const { return error == PackageError::None; }
};

enum class PackageStage {
    Verifying, ///< SHA-256 of the archive.
    Extracting,
    Checking, ///< Every listed file against the manifest.
};

/// Turns a downloaded launcher release into a staged, checked update.
///
/// In order: SHA-256 of the archive → the archive's structure and every name
/// (the same rules as for projects) → extraction → the manifest → every file
/// the manifest lists, against its size and hash. Nothing outside @p stagingDir
/// is written. A package that fails any step leaves nothing behind but a
/// staging folder, which the caller deletes.
namespace UpdatePackage {

PackageResult prepare(const PackageRequest& request, const std::atomic<bool>* cancel = nullptr,
    const std::function<void(PackageStage stage, qint64 done, qint64 total)>& progress = {});

} // namespace UpdatePackage

} // namespace launcher
