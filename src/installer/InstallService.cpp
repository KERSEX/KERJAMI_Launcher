#include "installer/InstallService.h"

#include "core/Logger.h"
#include "github/ReleaseParser.h"
#include "security/HashVerifier.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

using namespace Qt::StringLiterals;

namespace launcher {

InstallService::InstallService(GitHubClient& client, ReleaseManager& releases, DownloadManager& downloads,
    Installer& installer, Uninstaller& uninstaller, InstalledProjects& installed, const LibraryLayout& layout,
    QObject* parent)
    : QObject(parent)
    , m_client(client)
    , m_releases(releases)
    , m_downloads(downloads)
    , m_installer(installer)
    , m_uninstaller(uninstaller)
    , m_installed(installed)
    , m_layout(layout)
    , m_starter([this](const QString& id) { return startProject(m_layout, m_installed, id); })
{
    connect(&m_releases, &ReleaseManager::releaseReady, this, &InstallService::onReleaseReady);
    connect(&m_releases, &ReleaseManager::fetchFailed, this, &InstallService::onReleaseFailed);

    connect(&m_downloads, &DownloadManager::progressChanged, this, &InstallService::onDownloadProgress);
    connect(&m_downloads, &DownloadManager::stateChanged, this, &InstallService::onDownloadStateChanged);
    connect(&m_downloads, &DownloadManager::finished, this, &InstallService::onDownloadFinished);
    connect(&m_downloads, &DownloadManager::failed, this, &InstallService::onDownloadFailed);

    connect(&m_installer, &Installer::progress, this, &InstallService::onInstallerProgress);
    connect(&m_installer, &Installer::installed, this, [this](const QString& id) { onInstalled(id); });
    connect(&m_installer, &Installer::failed, this, &InstallService::onInstallFailed);
}

InstallService::~InstallService()
{
    // A verification in the background is stopped at the next block and waited for.
    for (Session& session : m_sessions) {
        if (session.verifyCancel)
            *session.verifyCancel = true;
    }
    for (QThread* thread : std::as_const(m_verifiers)) {
        thread->wait();
        delete thread;
    }
}

ProjectStatus InstallService::status(const QString& projectId) const
{
    const auto found = m_sessions.constFind(projectId);
    return found == m_sessions.cend() ? ProjectStatus() : found->status;
}

void InstallService::dismissFailure(const QString& projectId)
{
    if (m_failures.remove(projectId) > 0)
        emit statusChanged(projectId);
}

InstallService::Session* InstallService::session(const QString& projectId)
{
    const auto found = m_sessions.find(projectId);
    return found == m_sessions.end() ? nullptr : &found.value();
}

InstallService::Session* InstallService::sessionOfDownload(quint64 downloadId)
{
    if (downloadId == 0)
        return nullptr;
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it->downloadId == downloadId)
            return &it.value();
    }
    return nullptr;
}

void InstallService::setStage(Session& session, ProjectStatus::Stage stage, qint64 done, qint64 total)
{
    session.status.stage = stage;
    session.status.done = done;
    session.status.total = total;
    emit statusChanged(session.project.id);
}

// ---- install ---------------------------------------------------------------------

void InstallService::install(const Project& project, Mode mode)
{
    const QString id = project.id;
    if (m_sessions.contains(id) || m_installer.isBusy(id))
        return;

    m_failures.remove(id);
    Session fresh;
    fresh.project = project;
    fresh.mode = mode;
    fresh.status.stage = ProjectStatus::Stage::FetchingRelease;
    m_sessions.insert(id, fresh);
    emit statusChanged(id);

    qCInfo(lcInstall).noquote() << "Installing" << id << ": asking for the newest release";
    m_releases.fetchLatest(project);
}

void InstallService::onReleaseReady(const QString& projectId, const ReleaseSelection& selection)
{
    Session* current = session(projectId);
    // An update check of another part of the launcher also ends here; it is not ours.
    if (!current || current->status.stage != ProjectStatus::Stage::FetchingRelease)
        return;

    current->selection = selection;
    current->status.version = selection.release.version.toString();

    if (!selection.hasTrustedHashSource()) {
        OperationFailure failure;
        failure.kind = OperationFailure::Kind::NoHash;
        failure.detail = u"release %1 has no digest and no checksum file"_s.arg(selection.release.tag);
        fail(projectId, failure);
        return;
    }

    if (!selection.asset.sha256.isEmpty()) {
        current->expectedSha256 = selection.asset.sha256;
        startDownload(*current);
    } else {
        fetchChecksum(*current);
    }
}

void InstallService::onReleaseFailed(
    const QString& projectId, ReleaseManager::Problem problem, int httpStatus)
{
    Session* current = session(projectId);
    if (!current || current->status.stage != ProjectStatus::Stage::FetchingRelease)
        return;

    OperationFailure failure;
    failure.kind = OperationFailure::Kind::Release;
    failure.code = static_cast<int>(problem);
    failure.httpStatus = httpStatus;
    fail(projectId, failure);
}

void InstallService::fetchChecksum(Session& current)
{
    const ChecksumFile& file = *current.selection->checksumFile;
    setStage(current, ProjectStatus::Stage::FetchingChecksum);

    FetchOptions options;
    options.maxBytes = ReleaseParser::kMaxChecksumFileBytes;
    options.accept = "text/plain, application/octet-stream, */*";

    const QString id = current.project.id;
    GitHubRequest* request = m_client.get(file.url, options);
    current.checksumRequest = request;
    connect(request, &GitHubRequest::finished, this,
        [this, id](const NetworkResult& result) { onChecksum(id, result); });
}

void InstallService::onChecksum(const QString& projectId, const NetworkResult& result)
{
    Session* current = session(projectId);
    if (!current || current->status.stage != ProjectStatus::Stage::FetchingChecksum)
        return;
    if (current->cancelled || result.error == NetworkError::Aborted) {
        finishCancelled(projectId);
        return;
    }

    OperationFailure failure;
    failure.kind = OperationFailure::Kind::Checksum;
    if (!result.ok()) {
        failure.code = static_cast<int>(result.error);
        failure.httpStatus = result.httpStatus;
        failure.detail = result.detail;
        fail(projectId, failure);
        return;
    }

    const ChecksumFile& file = *current->selection->checksumFile;
    const QString hash =
        HashVerifier::findInChecksumFile(result.body, current->selection->asset.name, file.bareHashAllowed);
    if (hash.isEmpty()) {
        failure.detail =
            u"\"%1\" does not give a hash for \"%2\""_s.arg(file.name, current->selection->asset.name);
        fail(projectId, failure);
        return;
    }
    current->expectedSha256 = hash;
    startDownload(*current);
}

void InstallService::startDownload(Session& current)
{
    const ReleaseSelection& selection = *current.selection;
    const QString projectId = current.project.id;

    DownloadRequest request;
    request.projectId = projectId;
    request.title = current.project.name + u' ' + current.status.version;
    request.url = selection.downloadUrl;
    request.fileName = selection.asset.name;
    request.expectedSize = selection.asset.size;

    const DownloadId id = m_downloads.enqueue(request);
    current.downloadId = id;
    setStage(current, ProjectStatus::Stage::Downloading, 0, selection.asset.size);

    // A request that does not pass the download checks fails inside enqueue(),
    // before the id is known here.
    const std::optional<Download> download = m_downloads.download(id);
    if (download && download->state == DownloadState::Failed)
        onDownloadFailed(id);
}

void InstallService::onDownloadProgress(quint64 downloadId)
{
    Session* current = sessionOfDownload(downloadId);
    if (!current || current->status.stage != ProjectStatus::Stage::Downloading)
        return;
    const std::optional<Download> download = m_downloads.download(downloadId);
    if (!download)
        return;
    current->status.done = download->bytesReceived;
    current->status.total = download->bytesTotal;
    emit statusChanged(current->project.id);
}

void InstallService::onDownloadStateChanged(quint64 downloadId)
{
    Session* current = sessionOfDownload(downloadId);
    if (!current || current->status.stage != ProjectStatus::Stage::Downloading)
        return;
    const std::optional<Download> download = m_downloads.download(downloadId);
    // Cancelled by the user, here or on the downloads page.
    if (download && download->state == DownloadState::Cancelled)
        finishCancelled(current->project.id);
}

void InstallService::onDownloadFinished(quint64 downloadId)
{
    Session* current = sessionOfDownload(downloadId);
    if (!current || current->status.stage != ProjectStatus::Stage::Downloading)
        return;
    const std::optional<Download> download = m_downloads.download(downloadId);
    if (!download)
        return;

    current->archivePath = download->filePath;
    setStage(*current, ProjectStatus::Stage::Verifying, 0, download->bytesReceived);

    // To install at once, the installer verifies the hash itself. To wait for
    // the project to close, the archive is verified now, so that a bad file is
    // found (and discarded) when it arrives, not hours later.
    if (current->mode == Mode::PrepareOnly)
        startEarlyVerification(*current);
    else
        startInstaller(*current);
}

void InstallService::startInstaller(Session& current)
{
    InstallRequest request;
    request.project = current.project;
    request.version = current.selection->release.version.toString();
    request.tag = current.selection->release.tag;
    request.archivePath = current.archivePath;
    request.sha256 = current.expectedSha256;
    m_installer.install(request);
}

void InstallService::startEarlyVerification(Session& current)
{
    const QString id = current.project.id;
    const QString path = current.archivePath;
    const QString expected = current.expectedSha256;
    const qint64 total = QFileInfo(path).size();
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    current.verifyCancel = cancel;

    QThread* thread = QThread::create([this, id, path, expected, total, cancel] {
        const HashVerifier::VerifyResult result =
            HashVerifier::verifySha256(path, expected, cancel.get(), [this, id, total](qint64 done) {
                QMetaObject::invokeMethod(
                    this,
                    [this, id, done, total] {
                        Session* current = session(id);
                        if (current && current->verifyCancel && done < total) {
                            current->status.done = done;
                            current->status.total = total;
                            emit statusChanged(id);
                        }
                    },
                    Qt::QueuedConnection);
            });
        const int status = static_cast<int>(result.status);
        QMetaObject::invokeMethod(
            this, [this, id, status, detail = result.detail] { onEarlyVerified(id, status, detail); },
            Qt::QueuedConnection);
    });
    m_verifiers.append(thread);
    connect(thread, &QThread::finished, this, [this, thread] {
        m_verifiers.removeOne(thread);
        thread->deleteLater();
    });
    thread->start();
}

void InstallService::onEarlyVerified(const QString& projectId, int status, const QString& detail)
{
    Session* current = session(projectId);
    if (!current || !current->verifyCancel)
        return; // cancelled in the meantime
    current->verifyCancel.reset();

    const auto result = static_cast<HashVerifier::Status>(status);
    if (result == HashVerifier::Status::Match) {
        setStage(*current, ProjectStatus::Stage::Prepared);
        qCInfo(lcInstall).noquote() << "Update of" << projectId << "is prepared";
        emit prepared(projectId);
        return;
    }
    if (result == HashVerifier::Status::Cancelled) {
        finishCancelled(projectId);
        return;
    }

    OperationFailure failure;
    failure.kind = OperationFailure::Kind::Install;
    failure.code = static_cast<int>(result == HashVerifier::Status::Mismatch ? InstallError::HashMismatch
            : result == HashVerifier::Status::FileError                      ? InstallError::ArchiveUnreadable
                                                                             : InstallError::NoTrustedHash);
    failure.detail = detail;
    fail(projectId, failure);
}

void InstallService::installPrepared(const QString& projectId)
{
    Session* current = session(projectId);
    if (!current || current->status.stage != ProjectStatus::Stage::Prepared)
        return;
    qCInfo(lcInstall).noquote() << "Installing the prepared update of" << projectId;
    setStage(*current, ProjectStatus::Stage::Verifying, 0, QFileInfo(current->archivePath).size());
    startInstaller(*current);
}

void InstallService::onDownloadFailed(quint64 downloadId)
{
    Session* current = sessionOfDownload(downloadId);
    if (!current || current->status.stage != ProjectStatus::Stage::Downloading)
        return;
    const std::optional<Download> download = m_downloads.download(downloadId);
    if (!download)
        return;

    OperationFailure failure;
    failure.kind = OperationFailure::Kind::Download;
    failure.code = static_cast<int>(download->problem);
    failure.httpStatus = download->httpStatus;
    failure.detail = download->detail;
    fail(current->project.id, failure);
}

void InstallService::onInstallerProgress(
    const QString& projectId, Installer::Stage stage, qint64 done, qint64 total)
{
    Session* current = session(projectId);
    if (!current)
        return;
    ProjectStatus::Stage mapped = ProjectStatus::Stage::Verifying;
    switch (stage) {
    case Installer::Stage::Verifying:
        mapped = ProjectStatus::Stage::Verifying;
        break;
    case Installer::Stage::Extracting:
        mapped = ProjectStatus::Stage::Extracting;
        break;
    case Installer::Stage::Replacing:
        mapped = ProjectStatus::Stage::Replacing;
        break;
    }
    setStage(*current, mapped, done, total);
}

void InstallService::onInstalled(const QString& projectId)
{
    Session* current = session(projectId);
    if (!current)
        return;
    removeArchive(*current);
    finish(projectId, true);
}

void InstallService::onInstallFailed(const QString& projectId, const InstallResult& result)
{
    Session* current = session(projectId);
    if (!current)
        return;

    if (result.error == InstallError::Cancelled) {
        removeArchive(*current);
        finishCancelled(projectId);
        return;
    }

    OperationFailure failure;
    failure.kind = OperationFailure::Kind::Install;
    failure.code = static_cast<int>(result.error);
    failure.subcode = static_cast<int>(result.validationProblem);
    failure.detail = result.detail;
    fail(projectId, failure);
}

// ---- cancel, finish ----------------------------------------------------------------

void InstallService::cancel(const QString& projectId)
{
    Session* current = session(projectId);
    if (!current)
        return;

    switch (current->status.stage) {
    case ProjectStatus::Stage::Idle:
        break;
    case ProjectStatus::Stage::FetchingRelease:
        // The answer of GitHub is ignored when it arrives.
        finishCancelled(projectId);
        break;
    case ProjectStatus::Stage::FetchingChecksum:
        current->cancelled = true;
        if (current->checksumRequest)
            current->checksumRequest->abort(); // reports back through onChecksum()
        else
            finishCancelled(projectId);
        break;
    case ProjectStatus::Stage::Downloading:
        m_downloads.cancel(current->downloadId); // reports back through onDownloadStateChanged()
        break;
    case ProjectStatus::Stage::Verifying:
    case ProjectStatus::Stage::Extracting:
        if (current->verifyCancel)
            *current->verifyCancel = true; // the early verification reports back through onEarlyVerified()
        else
            m_installer.cancel(projectId); // reports back through onInstallFailed()
        break;
    case ProjectStatus::Stage::Prepared:
        finishCancelled(projectId); // removes the archive
        break;
    case ProjectStatus::Stage::Replacing:
        break; // too late: the installed version is being replaced
    }
}

void InstallService::fail(const QString& projectId, const OperationFailure& failure)
{
    qCWarning(lcInstall).noquote() << "Installing" << projectId << "failed:" << int(failure.kind)
                                   << failure.code << failure.httpStatus << failure.detail;
    if (Session* current = session(projectId))
        removeArchive(*current);
    m_sessions.remove(projectId);
    m_failures.insert(projectId, failure);
    emit statusChanged(projectId);
    emit installFinished(projectId, false);
}

void InstallService::finishCancelled(const QString& projectId)
{
    qCInfo(lcInstall).noquote() << "Installing" << projectId << "was cancelled";
    if (Session* current = session(projectId))
        removeArchive(*current);
    m_sessions.remove(projectId);
    emit statusChanged(projectId);
    emit installFinished(projectId, false);
}

void InstallService::finish(const QString& projectId, bool success)
{
    m_sessions.remove(projectId);
    emit statusChanged(projectId);
    emit installFinished(projectId, success);
}

void InstallService::removeArchive(Session& current)
{
    // The archive has done its job or cannot be used; a new attempt downloads it again.
    if (current.archivePath.isEmpty())
        return;
    QFile::remove(current.archivePath);
    QDir().rmdir(QFileInfo(current.archivePath).absolutePath()); // only succeeds if the folder is empty
    current.archivePath.clear();
}

// ---- uninstall, start ----------------------------------------------------------------

UninstallResult InstallService::uninstall(const QString& projectId)
{
    UninstallResult result;
    if (m_sessions.contains(projectId) || m_installer.isBusy(projectId)) {
        result.error = UninstallError::Busy;
        result.detail = u"an installation is running"_s;
    } else {
        result = m_uninstaller.uninstall(projectId);
    }

    if (result.ok()) {
        m_failures.remove(projectId);
    } else {
        OperationFailure failure;
        failure.kind = OperationFailure::Kind::Uninstall;
        failure.code = static_cast<int>(result.error);
        failure.detail = result.detail;
        m_failures.insert(projectId, failure);
    }
    emit statusChanged(projectId);
    return result;
}

StartResult InstallService::start(const QString& projectId)
{
    StartResult result;
    if (m_sessions.contains(projectId) || m_installer.isBusy(projectId)) {
        result.error = StartError::Busy;
        result.detail = u"an installation is running"_s;
    } else {
        result = m_starter(projectId);
    }

    if (result.ok()) {
        m_failures.remove(projectId);
    } else {
        OperationFailure failure;
        failure.kind = OperationFailure::Kind::Start;
        failure.code = static_cast<int>(result.error);
        failure.detail = result.detail;
        m_failures.insert(projectId, failure);
    }
    emit statusChanged(projectId);
    return result;
}

} // namespace launcher
