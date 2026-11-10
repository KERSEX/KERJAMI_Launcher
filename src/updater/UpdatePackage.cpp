#include "updater/UpdatePackage.h"

#include "installer/ArchiveExtractor.h"
#include "installer/FileOps.h"
#include "security/HashVerifier.h"
#include "security/ZipArchive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher::UpdatePackage {

namespace {

constexpr int kMaxWindowsPath = 259;
constexpr int kMinPathBudget = 16;
constexpr qint64 kSpaceReserve = 16LL * 1024 * 1024;
constexpr qint64 kMaxManifestBytes = 4 * 1024 * 1024;

PackageResult failure(PackageError error, const QString& detail)
{
    PackageResult result;
    result.error = error;
    result.detail = detail;
    return result;
}

qint64 defaultAvailableSpace(const QString& folder)
{
    const QStorageInfo storage(folder);
    return storage.isValid() ? storage.bytesAvailable() : -1;
}

} // namespace

PackageResult prepare(const PackageRequest& request, const std::atomic<bool>* cancel,
    const std::function<void(PackageStage, qint64, qint64)>& progress)
{
    const auto report = [&progress](PackageStage stage, qint64 done, qint64 total) {
        if (progress)
            progress(stage, done, total);
    };
    const auto cancelled = [cancel] { return cancel != nullptr && cancel->load(); };

    // --- 1. the archive is what the release announced ------------------------------------
    const QString expected = request.expectedSha256.trimmed();
    if (!HashVerifier::isValidSha256(expected))
        return failure(PackageError::NoTrustedHash, u"no valid SHA-256 for the archive"_s);
    if (!QFileInfo(request.archivePath).isFile())
        return failure(PackageError::ArchiveUnreadable, u"the archive file is missing"_s);

    const qint64 archiveSize = QFileInfo(request.archivePath).size();
    const HashVerifier::VerifyResult hash = HashVerifier::verifySha256(request.archivePath, expected, cancel,
        [&](qint64 done) { report(PackageStage::Verifying, done, archiveSize); });
    switch (hash.status) {
    case HashVerifier::Status::Match:
        break;
    case HashVerifier::Status::Mismatch:
        return failure(
            PackageError::HashMismatch, u"expected %1, got %2"_s.arg(expected.toLower(), hash.actual));
    case HashVerifier::Status::Cancelled:
        return failure(PackageError::Cancelled, {});
    case HashVerifier::Status::FileError:
        return failure(PackageError::ArchiveUnreadable, hash.detail);
    case HashVerifier::Status::NoExpectedHash:
    case HashVerifier::Status::InvalidExpectedHash:
        return failure(PackageError::NoTrustedHash, u"no valid SHA-256 for the archive"_s);
    }

    // --- 2. structure and names of the archive ---------------------------------------------
    const QString staging = QDir::cleanPath(request.stagingDir);
    const QString root = QDir::cleanPath(request.rootDir);

    // The same relative path must fit below the staging folder and below the launcher's folder.
    ArchiveValidator::Limits limits = request.limits;
    const int budget = kMaxWindowsPath - static_cast<int>(std::max(staging.size(), root.size())) - 1;
    if (budget < kMinPathBudget)
        return failure(PackageError::PathTooLong, u"only %1 characters are left for paths"_s.arg(budget));
    limits.maxPathLength = std::min(limits.maxPathLength, budget);

    ZipArchive::OpenResult opened = ZipArchive::open(request.archivePath);
    if (!opened.ok())
        return failure(PackageError::ArchiveUnreadable, opened.detail);
    ZipArchive& archive = *opened.archive;

    const ArchiveValidator::ValidationResult validation =
        ArchiveValidator::validate(archive, limits, UpdateManifestFormat::kFileName);
    if (!validation.ok()) {
        PackageResult result = failure(validation.problem == ArchiveValidator::Problem::MissingExecutable
                ? PackageError::ManifestMissing
                : PackageError::ArchiveRejected,
            validation.detail);
        result.validationProblem = validation.problem;
        return result;
    }

    // --- 3. extraction --------------------------------------------------------------------------
    const qint64 needed = validation.totalSize + kSpaceReserve;
    const qint64 available = request.availableSpace
        ? request.availableSpace(staging)
        : defaultAvailableSpace(QFileInfo(staging).absolutePath());
    if (available >= 0 && available < needed)
        return failure(
            PackageError::InsufficientSpace, u"%1 bytes needed, %2 free"_s.arg(needed).arg(available));

    QString error;
    if (!FileOps::removeTree(staging, &error))
        return failure(
            PackageError::ExtractionFailed, u"the staging folder cannot be cleared: %1"_s.arg(error));
    if (!QDir().mkpath(staging))
        return failure(PackageError::ExtractionFailed, u"the staging folder cannot be created"_s);

    const ArchiveExtractor::Result extracted = ArchiveExtractor::extract(archive, validation, staging, cancel,
        [&](qint64 done, qint64 total) { report(PackageStage::Extracting, done, total); });
    if (!extracted.ok()) {
        return extracted.status == ArchiveExtractor::Status::Cancelled
            ? failure(PackageError::Cancelled, {})
            : failure(PackageError::ExtractionFailed, extracted.detail);
    }

    // --- 4. the manifest ----------------------------------------------------------------------------
    QFile manifestFile(staging + u'/' + UpdateManifestFormat::kFileName);
    if (!manifestFile.open(QIODevice::ReadOnly) || manifestFile.size() > kMaxManifestBytes)
        return failure(PackageError::ManifestMissing, u"the manifest cannot be read"_s);
    const ManifestParseResult parsed =
        UpdateManifestFormat::parse(manifestFile.readAll(), request.expectations);
    if (!parsed.ok)
        return failure(PackageError::ManifestInvalid, parsed.error);
    if (parsed.manifest.version != request.expectedVersion)
        return failure(PackageError::VersionMismatch,
            u"the package says %1, the release is %2"_s.arg(
                parsed.manifest.version, request.expectedVersion));

    // --- 5. every listed file is what the manifest says ----------------------------------------------
    qint64 checked = 0;
    qint64 total = 0;
    for (const ManifestFile& file : parsed.manifest.files)
        total += file.size;
    for (const ManifestFile& file : parsed.manifest.files) {
        if (cancelled())
            return failure(PackageError::Cancelled, {});
        const QString path = staging + u'/' + file.path;
        const QFileInfo info(path);
        if (!info.isFile() || FileOps::isLink(path) || info.size() != file.size)
            return failure(
                PackageError::FilesMismatch, u"\"%1\" is missing or has another size"_s.arg(file.path));
        if (HashVerifier::sha256File(path, nullptr, cancel) != file.sha256) {
            return cancelled()
                ? failure(PackageError::Cancelled, {})
                : failure(PackageError::FilesMismatch, u"\"%1\" does not match its hash"_s.arg(file.path));
        }
        checked += file.size;
        report(PackageStage::Checking, checked, total);
    }

    PackageResult result;
    result.manifest = parsed.manifest;
    return result;
}

} // namespace launcher::UpdatePackage
