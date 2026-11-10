#include "installer/Installer.h"

#include "core/Logger.h"
#include "core/PathManager.h"
#include "core/SemVer.h"
#include "github/RegistryParser.h"
#include "installer/ArchiveExtractor.h"
#include "installer/FileOps.h"
#include "security/HashVerifier.h"
#include "security/ZipArchive.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStorageInfo>
#include <QThread>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

/// Windows limits a path to 260 characters (MAX_PATH). Programs that are not
/// written for long paths, which is most of them, fail beyond it.
constexpr int kMaxWindowsPath = 259;
constexpr int kMinPathBudget = 16;

qint64 defaultAvailableSpace(const QString& folder)
{
    const QStorageInfo storage(folder);
    return storage.isValid() ? storage.bytesAvailable() : -1;
}

InstallResult failure(InstallError error, const QString& detail)
{
    InstallResult result;
    result.error = error;
    result.detail = detail;
    return result;
}

} // namespace

struct Installer::Job {
    InstallRequest request;
    std::atomic<bool> cancel = false;
    std::atomic<bool> committed = false; ///< The installed version is being replaced; too late to cancel.
    InstallResult result;
    QThread* thread = nullptr;
};

Installer::Installer(const LibraryLayout& layout, InstalledProjects& installed, QObject* parent)
    : QObject(parent)
    , m_layout(layout)
    , m_installed(installed)
    , m_availableSpace(defaultAvailableSpace)
{
    qRegisterMetaType<launcher::InstallResult>();
    qRegisterMetaType<launcher::Installer::Stage>();
}

Installer::~Installer()
{
    // Workers write to the library; they are stopped at the next safe point
    // and waited for, never abandoned.
    for (const std::shared_ptr<Job>& job : std::as_const(m_jobs)) {
        job->cancel = true;
        job->thread->wait();
        delete job->thread;
    }
}

void Installer::install(const InstallRequest& request)
{
    const QString id = request.project.id;
    if (m_jobs.contains(id)) {
        emit failed(id, failure(InstallError::Busy, u"an installation of this project is already running"_s));
        return;
    }

    auto job = std::make_shared<Job>();
    job->request = request;
    job->thread = QThread::create([this, job] { job->result = run(*job); });
    m_jobs.insert(id, job);

    connect(job->thread, &QThread::finished, this, [this, id] { onFinished(id); });
    job->thread->start();
}

void Installer::cancel(const QString& projectId)
{
    const auto it = m_jobs.constFind(projectId);
    if (it != m_jobs.cend() && !(*it)->committed)
        (*it)->cancel = true;
}

void Installer::onFinished(const QString& projectId)
{
    const std::shared_ptr<Job> job = m_jobs.take(projectId);
    if (!job)
        return;
    job->thread->deleteLater();

    if (job->result.ok()) {
        qCInfo(lcInstall).noquote() << "Installed" << projectId << job->request.version;
        emit installed(projectId, job->result);
    } else {
        qCWarning(lcInstall).noquote()
            << "Installation of" << projectId << "failed:" << static_cast<int>(job->result.error)
            << job->result.detail;
        emit failed(projectId, job->result);
    }
}

InstallResult Installer::run(Job& job)
{
    const InstallRequest& request = job.request;
    const Project& project = request.project;
    const QString id = project.id;

    // Throttled, from the worker thread to the receivers in the main thread.
    QElapsedTimer clock;
    clock.start();
    qint64 lastEmit = -1000;
    const auto report = [&](Stage stage, qint64 done, qint64 total) {
        const qint64 now = clock.elapsed();
        if (now - lastEmit < 100 && done < total)
            return;
        lastEmit = now;
        QMetaObject::invokeMethod(
            this, [this, id, stage, done, total] { emit progress(id, stage, done, total); },
            Qt::QueuedConnection);
    };

    // --- 1. the request itself ---------------------------------------------
    if (!RegistryParser::isValidProjectId(project.id)
        || !RegistryParser::isValidInstallDirectory(project.installDirectory)
        || !RegistryParser::isValidExecutablePath(project.executable))
        return failure(InstallError::InvalidProject, u"project id, folder or start file are not valid"_s);

    const std::optional<SemVer> version = SemVer::parse(request.version);
    if (!version)
        return failure(InstallError::InvalidProject, u"version \"%1\" is not valid"_s.arg(request.version));
    const QString versionText = version->toString();

    // An archive without a hash the launcher can compare against is never
    // installed. There is no switch for it: a launcher that downloads programs
    // from the internet must not offer "install anyway".
    const QString expectedHash = request.sha256.trimmed();
    if (!HashVerifier::isValidSha256(expectedHash))
        return failure(InstallError::NoTrustedHash, u"no valid SHA-256 for the archive"_s);

    if (!QFileInfo(request.archivePath).isFile())
        return failure(InstallError::ArchiveUnreadable, u"archive file is missing"_s);

    if (m_isRunning && m_isRunning(id))
        return failure(InstallError::ProjectRunning, u"the project is running"_s);

    // --- 2. integrity ---------------------------------------------------------
    const qint64 archiveSize = QFileInfo(request.archivePath).size();
    const HashVerifier::VerifyResult hash = HashVerifier::verifySha256(request.archivePath, expectedHash,
        &job.cancel, [&](qint64 done) { report(Stage::Verifying, done, archiveSize); });
    switch (hash.status) {
    case HashVerifier::Status::Match:
        break;
    case HashVerifier::Status::Mismatch:
        return failure(
            InstallError::HashMismatch, u"expected %1, got %2"_s.arg(expectedHash.toLower(), hash.actual));
    case HashVerifier::Status::Cancelled:
        return failure(InstallError::Cancelled, {});
    case HashVerifier::Status::FileError:
        return failure(InstallError::ArchiveUnreadable, hash.detail);
    case HashVerifier::Status::NoExpectedHash:
    case HashVerifier::Status::InvalidExpectedHash:
        return failure(InstallError::NoTrustedHash, u"no valid SHA-256 for the archive"_s);
    }

    // --- 3. archive structure and contents -----------------------------------
    const QString staging = m_layout.stagingDir(id);
    const QString current = m_layout.currentDir(project.installDirectory);

    ArchiveValidator::Limits limits = m_limits;
    const int budget = kMaxWindowsPath - int(std::max(staging.size(), current.size())) - 1;
    if (budget < kMinPathBudget)
        return failure(InstallError::PathTooLong,
            u"only %1 characters are left for paths inside the project"_s.arg(budget));
    limits.maxPathLength = std::min(limits.maxPathLength, budget);

    ZipArchive::OpenResult opened = ZipArchive::open(request.archivePath);
    if (!opened.ok())
        return failure(InstallError::ArchiveUnreadable, opened.detail);
    ZipArchive& archive = *opened.archive;

    const ArchiveValidator::ValidationResult validation =
        ArchiveValidator::validate(archive, limits, project.executable);
    if (!validation.ok()) {
        InstallResult result = failure(InstallError::ArchiveRejected, validation.detail);
        result.validationProblem = validation.problem;
        return result;
    }

    // --- 4. extraction into staging ------------------------------------------
    const qint64 needed = validation.totalSize + kSpaceReserve;
    if (m_availableSpace) {
        const qint64 available = m_availableSpace(m_layout.tempDir());
        if (available >= 0 && available < needed)
            return failure(
                InstallError::InsufficientSpace, u"%1 bytes needed, %2 free"_s.arg(needed).arg(available));
    }

    QString error;
    if (!FileOps::removeTree(staging, &error))
        return failure(
            InstallError::ExtractionFailed, u"old staging folder cannot be removed: %1"_s.arg(error));
    if (!QDir().mkpath(staging))
        return failure(InstallError::ExtractionFailed, u"staging folder cannot be created"_s);

    const auto discardStaging = [&staging] {
        QString ignored;
        FileOps::removeTree(staging, &ignored);
    };

    const ArchiveExtractor::Result extracted = ArchiveExtractor::extract(archive, validation, staging,
        &job.cancel, [&](qint64 done, qint64 total) { report(Stage::Extracting, done, total); });
    if (!extracted.ok()) {
        discardStaging();
        if (extracted.status == ArchiveExtractor::Status::Cancelled)
            return failure(InstallError::Cancelled, {});
        return failure(InstallError::ExtractionFailed, extracted.detail);
    }

    const QFileInfo startFile(staging + u'/' + project.executable);
    if (!startFile.isFile() || FileOps::isLink(startFile.filePath())) {
        discardStaging();
        return failure(InstallError::ExtractionFailed,
            u"start file \"%1\" is missing after extraction"_s.arg(project.executable));
    }

    if (job.cancel) {
        discardStaging();
        return failure(InstallError::Cancelled, {});
    }
    job.committed = true;
    report(Stage::Replacing, 0, 0);

    // --- 5. move the installed version away ----------------------------------
    const QString projectDir = m_layout.projectDir(project.installDirectory);
    if (!PathManager::isInside(m_layout.libraryDir(), projectDir) || FileOps::isLink(projectDir)) {
        discardStaging();
        return failure(InstallError::SwapFailed, u"the project folder is outside the library or a link"_s);
    }
    if (!QDir().mkpath(projectDir) || FileOps::isLink(current)) {
        discardStaging();
        return failure(InstallError::SwapFailed, u"the project folder cannot be prepared"_s);
    }

    QString backupDir;
    if (QFileInfo::exists(current)) {
        const std::optional<InstalledProject> previous = m_installed.get(id);
        const QString backups = m_layout.backupsDir(project.installDirectory);
        if (!QDir().mkpath(backups)) {
            discardStaging();
            return failure(InstallError::SwapFailed, u"the backup folder cannot be created"_s);
        }

        const QString baseName = previous ? previous->version : u"unknown"_s;
        // The folder is named after the version. The same version again (a
        // reinstall) or an unknown one gets a time stamp so nothing is overwritten.
        backupDir = backups + u'/' + baseName;
        if (QFileInfo::exists(backupDir)) {
            const QString stamped = backups + u'/' + baseName + u'-'
                + QDateTime::currentDateTimeUtc().toString(u"yyyyMMddHHmmss"_s);
            backupDir = stamped;
            for (int n = 2; QFileInfo::exists(backupDir); ++n)
                backupDir = stamped + u'-' + QString::number(n);
        }

        // A folder with a running program inside cannot be renamed on Windows.
        // That refusal is the launcher's protection against replacing a program
        // while it is in use.
        if (!FileOps::renameWithRetry(current, backupDir, &error)) {
            discardStaging();
            return failure(InstallError::ProjectRunning, error);
        }
    }

    // --- 6. move the new version into place ----------------------------------
    if (!FileOps::renameWithRetry(staging, current, &error)) {
        if (!backupDir.isEmpty() && !FileOps::renameWithRetry(backupDir, current, &error))
            return failure(InstallError::RollbackFailed,
                u"the previous version is in \"%1\" and could not be moved back: %2"_s.arg(backupDir, error));
        discardStaging();
        return failure(InstallError::SwapFailed, u"the new version could not be moved into place"_s);
    }

    // --- 7. register ---------------------------------------------------------
    InstalledProject record;
    record.projectId = id;
    record.version = versionText;
    record.tag = request.tag;
    record.installDirectory = project.installDirectory;
    record.executable = project.executable;
    record.installedAt = QDateTime::currentDateTimeUtc();
    if (const auto previous = m_installed.get(id))
        record.lastStartedAt = previous->lastStartedAt;

    QString saveError;
    if (!m_installed.upsert(record, &saveError)) {
        QString undoError;
        bool undone = FileOps::renameWithRetry(current, staging, &undoError);
        if (undone && !backupDir.isEmpty())
            undone = FileOps::renameWithRetry(backupDir, current, &undoError);
        if (!undone)
            return failure(InstallError::RollbackFailed,
                u"the list of installed projects could not be saved (%1) and going back failed: %2"_s.arg(
                    saveError, undoError));
        discardStaging();
        QDir().rmdir(m_layout.backupsDir(project.installDirectory));
        QDir().rmdir(projectDir);
        return failure(InstallError::RegistrationFailed, saveError);
    }

    // --- done: keep only the newest backups ----------------------------------
    const QDir backups(m_layout.backupsDir(project.installDirectory));
    QFileInfoList old = backups.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (qsizetype i = kKeptBackups; i < old.size(); ++i) {
        QString pruneError;
        if (!FileOps::removeTree(old.at(i).filePath(), &pruneError))
            qCWarning(lcInstall).noquote() << "Old backup could not be removed:" << pruneError;
    }

    InstallResult result;
    result.backupDirectory = backupDir;
    return result;
}

} // namespace launcher
