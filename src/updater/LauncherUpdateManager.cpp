#include "updater/LauncherUpdateManager.h"

#include "app/Branding.h"
#include "core/JsonFile.h"
#include "core/Logger.h"
#include "core/SemVer.h"
#include "github/RegistryParser.h"
#include "github/ReleaseParser.h"
#include "installer/FileOps.h"
#include "security/HashVerifier.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

constexpr int kFirstCheckDelayMs = 3000;
constexpr int kRateLimitReserve = 10;

} // namespace

LauncherUpdateManager::LauncherUpdateManager(GitHubClient& client, ReleaseManager& releases,
    DownloadManager& downloads, SettingsManager& settings, const PathManager& paths, QObject* parent)
    : QObject(parent)
    , m_client(client)
    , m_releases(releases)
    , m_downloads(downloads)
    , m_settings(settings)
    , m_paths(paths)
    , m_currentVersion(branding::version())
    , m_runningFile(QFileInfo(QCoreApplication::applicationFilePath()).fileName())
    , m_expectations {branding::productName(), branding::executableFileName(), branding::updaterFileName()}
    , m_processId(QCoreApplication::applicationPid())
{
    qRegisterMetaType<launcher::PackageResult>();

    m_startUpdater = [](const QString& program, const QString& jobFile) {
        return QProcess::startDetached(program, {u"--job"_s, jobFile}, QFileInfo(program).absolutePath());
    };
    m_info.currentVersion = m_currentVersion;

    m_timer.setInterval(m_settings.settings().updateCheckIntervalMinutes * 60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_settings.settings().launcherUpdateAutoCheck)
            checkNow();
    });
    connect(&m_settings, &SettingsManager::settingsChanged, this,
        [this] { m_timer.setInterval(m_settings.settings().updateCheckIntervalMinutes * 60 * 1000); });

    connect(&m_releases, &ReleaseManager::releaseReady, this, &LauncherUpdateManager::onReleaseReady);
    connect(&m_releases, &ReleaseManager::fetchFailed, this, &LauncherUpdateManager::onReleaseFailed);
    connect(
        &m_downloads, &DownloadManager::progressChanged, this, &LauncherUpdateManager::onDownloadProgress);
    connect(
        &m_downloads, &DownloadManager::stateChanged, this, &LauncherUpdateManager::onDownloadStateChanged);
    connect(&m_downloads, &DownloadManager::finished, this, &LauncherUpdateManager::onDownloadFinished);
    connect(&m_downloads, &DownloadManager::failed, this, &LauncherUpdateManager::onDownloadFailed);
}

LauncherUpdateManager::~LauncherUpdateManager()
{
    if (m_pending && m_pending->cancel)
        *m_pending->cancel = true;
    for (QThread* thread : std::as_const(m_threads)) {
        thread->wait();
        delete thread;
    }
}

LauncherUpdateInfo LauncherUpdateManager::info() const
{
    return m_info;
}

Project LauncherUpdateManager::launcherProject() const
{
    // The launcher's releases are fetched like a project's: the same checks for the
    // owner, the repository and the asset name apply. The id is reserved for it.
    Project project;
    project.id = RegistryParser::kLauncherProjectId;
    project.name = branding::productName();
    project.description = branding::displayName();
    project.owner = branding::gitHubOwner();
    project.repository = branding::launcherRepository();
    project.category = u"Launcher"_s;
    project.assetPattern = branding::updateAssetPattern();
    project.executable = branding::executableFileName();
    project.installDirectory = u"launcher"_s;
    return project;
}

void LauncherUpdateManager::setState(LauncherUpdateState state)
{
    m_info.state = state;
    emit infoChanged();
}

void LauncherUpdateManager::start()
{
    m_timer.start();
    QTimer::singleShot(kFirstCheckDelayMs, this, [this] {
        if (m_settings.settings().launcherUpdateAutoCheck)
            checkNow();
    });
}

// ---- looking for a newer version -----------------------------------------------------------

void LauncherUpdateManager::checkNow()
{
    using State = LauncherUpdateState;
    if (m_checking || m_info.state == State::Downloading || m_info.state == State::Preparing
        || m_info.state == State::Ready || m_info.state == State::Applying)
        return;

    // One request; if the budget is nearly used up, projects need the rest.
    const RateLimit limit = m_client.rateLimit();
    if (limit.isKnown() && limit.remaining < kRateLimitReserve) {
        qCInfo(lcUpdate) << "Skipping the launcher update check:" << limit.remaining << "requests left";
        return;
    }

    m_checking = true;
    m_stateBeforeCheck = m_info.state;
    qCInfo(lcUpdate) << "Looking for a newer launcher version";
    setState(State::Checking);
    m_releases.fetchLatest(launcherProject());
}

void LauncherUpdateManager::onReleaseReady(const QString& projectId, const ReleaseSelection& selection)
{
    if (projectId != RegistryParser::kLauncherProjectId || !m_checking)
        return;
    m_checking = false;

    m_latest = selection;
    m_info.latestVersion = selection.release.version.toString();
    m_info.tag = selection.release.tag;
    m_info.notes = selection.release.notes;
    m_info.publishedAt = selection.release.publishedAt;
    m_info.assetSize = selection.asset.size;
    m_info.checkedAt = QDateTime::currentDateTimeUtc();
    m_info.lastCheckFailed = false;
    m_info.failure = {};

    const std::optional<SemVer> current = SemVer::parse(m_currentVersion);
    const bool newer = current && selection.release.version > *current;
    qCInfo(lcUpdate).noquote() << "Newest launcher release is" << m_info.latestVersion << "- running"
                               << m_currentVersion << (newer ? "(update available)" : "");
    setState(newer ? LauncherUpdateState::Available : LauncherUpdateState::UpToDate);
}

void LauncherUpdateManager::onReleaseFailed(const QString& projectId, ReleaseManager::Problem, int)
{
    if (projectId != RegistryParser::kLauncherProjectId || !m_checking)
        return;
    m_checking = false;
    // A failed look says nothing about the launcher's version: what was known stays known.
    m_info.lastCheckFailed = true;
    setState(m_stateBeforeCheck);
}

// ---- the user's decision: download and prepare ----------------------------------------------------

void LauncherUpdateManager::install()
{
    // A failed attempt may be tried again: the newer version is still known.
    const bool retry = m_info.state == LauncherUpdateState::Failed && m_latest;
    if ((m_info.state != LauncherUpdateState::Available && !retry) || !m_latest)
        return;

    if (busy()) {
        m_info.failure = {LauncherUpdateFailure::Kind::Busy, 0, 0, 0, u"projects are being installed"_s};
        emit infoChanged();
        return;
    }

    m_info.failure = {};
    Pending pending;
    pending.selection = *m_latest;
    pending.cancel = std::make_shared<std::atomic<bool>>(false);
    pending.stagingDir = m_paths.tempDir() + u"/update-"_s + pending.selection.release.version.toString();
    pending.restartWhenReady = true;
    m_pending = pending;
    m_info.done = 0;
    m_info.total = pending.selection.asset.size;
    qCInfo(lcUpdate).noquote() << "Updating the launcher to" << m_info.latestVersion;
    setState(LauncherUpdateState::Downloading);

    if (!pending.selection.hasTrustedHashSource()) {
        LauncherUpdateFailure failure;
        failure.kind = LauncherUpdateFailure::Kind::NoHash;
        failure.detail =
            u"release %1 has no digest and no checksum file"_s.arg(pending.selection.release.tag);
        fail(failure);
        return;
    }
    if (!pending.selection.asset.sha256.isEmpty()) {
        m_pending->expectedSha256 = pending.selection.asset.sha256;
        startDownload();
    } else {
        fetchChecksum();
    }
}

void LauncherUpdateManager::fetchChecksum()
{
    const ChecksumFile& file = *m_pending->selection.checksumFile;
    FetchOptions options;
    options.maxBytes = ReleaseParser::kMaxChecksumFileBytes;
    options.accept = "text/plain, application/octet-stream, */*";
    GitHubRequest* request = m_client.get(file.url, options);
    m_checksumRequest = request;
    connect(request, &GitHubRequest::finished, this, &LauncherUpdateManager::onChecksum);
}

void LauncherUpdateManager::onChecksum(const NetworkResult& result)
{
    if (!m_pending || m_info.state != LauncherUpdateState::Downloading)
        return;
    if (result.error == NetworkError::Aborted)
        return; // cancelled

    LauncherUpdateFailure failure;
    failure.kind = LauncherUpdateFailure::Kind::Checksum;
    if (!result.ok()) {
        failure.code = static_cast<int>(result.error);
        failure.httpStatus = result.httpStatus;
        failure.detail = result.detail;
        fail(failure);
        return;
    }
    const ChecksumFile& file = *m_pending->selection.checksumFile;
    const QString hash =
        HashVerifier::findInChecksumFile(result.body, m_pending->selection.asset.name, file.bareHashAllowed);
    if (hash.isEmpty()) {
        failure.detail =
            u"\"%1\" does not give a hash for \"%2\""_s.arg(file.name, m_pending->selection.asset.name);
        fail(failure);
        return;
    }
    m_pending->expectedSha256 = hash;
    startDownload();
}

void LauncherUpdateManager::startDownload()
{
    const ReleaseSelection& selection = m_pending->selection;
    DownloadRequest request;
    request.projectId = RegistryParser::kLauncherProjectId;
    request.title = branding::productName() + u' ' + selection.release.version.toString();
    request.url = selection.downloadUrl;
    request.fileName = selection.asset.name;
    request.expectedSize = selection.asset.size;

    const DownloadId id = m_downloads.enqueue(request);
    m_pending->downloadId = id;
    // A request that does not pass the download checks fails inside enqueue(), before the id is known here.
    const std::optional<Download> download = m_downloads.download(id);
    if (download && download->state == DownloadState::Failed)
        onDownloadFailed(id);
}

void LauncherUpdateManager::onDownloadProgress(quint64 id)
{
    if (!m_pending || m_pending->downloadId != id || m_info.state != LauncherUpdateState::Downloading)
        return;
    const std::optional<Download> download = m_downloads.download(id);
    if (!download)
        return;
    m_info.done = download->bytesReceived;
    m_info.total = download->bytesTotal;
    emit infoChanged();
}

void LauncherUpdateManager::onDownloadStateChanged(quint64 id)
{
    if (!m_pending || m_pending->downloadId != id || m_info.state != LauncherUpdateState::Downloading)
        return;
    const std::optional<Download> download = m_downloads.download(id);
    if (download && download->state == DownloadState::Cancelled) {
        // Cancelled on the downloads page: the version stays announced.
        m_pending.reset();
        setState(LauncherUpdateState::Available);
    }
}

void LauncherUpdateManager::onDownloadFinished(quint64 id)
{
    if (!m_pending || m_pending->downloadId != id || m_info.state != LauncherUpdateState::Downloading)
        return;
    const std::optional<Download> download = m_downloads.download(id);
    if (!download)
        return;
    m_pending->archivePath = download->filePath;
    startPreparation();
}

void LauncherUpdateManager::onDownloadFailed(quint64 id)
{
    if (!m_pending || m_pending->downloadId != id || m_info.state != LauncherUpdateState::Downloading)
        return;
    const std::optional<Download> download = m_downloads.download(id);
    if (!download)
        return;
    LauncherUpdateFailure failure;
    failure.kind = LauncherUpdateFailure::Kind::Download;
    failure.code = static_cast<int>(download->problem);
    failure.httpStatus = download->httpStatus;
    failure.detail = download->detail;
    fail(failure);
}

void LauncherUpdateManager::startPreparation()
{
    m_info.done = 0;
    m_info.total = m_pending->selection.asset.size;
    setState(LauncherUpdateState::Preparing);

    PackageRequest request;
    request.archivePath = m_pending->archivePath;
    request.expectedSha256 = m_pending->expectedSha256;
    request.stagingDir = m_pending->stagingDir;
    request.rootDir = m_paths.root();
    request.expectedVersion = m_pending->selection.release.version.toString();
    request.expectations = m_expectations;

    const std::shared_ptr<std::atomic<bool>> cancel = m_pending->cancel;
    QThread* thread = QThread::create([this, request, cancel] {
        const PackageResult result =
            UpdatePackage::prepare(request, cancel.get(), [this](PackageStage, qint64 done, qint64 total) {
                QMetaObject::invokeMethod(
                    this,
                    [this, done, total] {
                        if (m_info.state == LauncherUpdateState::Preparing) {
                            m_info.done = done;
                            m_info.total = total;
                            emit infoChanged();
                        }
                    },
                    Qt::QueuedConnection);
            });
        QMetaObject::invokeMethod(this, [this, result] { onPrepared(result); }, Qt::QueuedConnection);
    });
    m_threads.append(thread);
    connect(thread, &QThread::finished, this, [this, thread] {
        m_threads.removeOne(thread);
        thread->deleteLater();
    });
    thread->start();
}

void LauncherUpdateManager::onPrepared(const PackageResult& result)
{
    if (!m_pending || m_info.state != LauncherUpdateState::Preparing)
        return; // cancelled in the meantime
    removeArchive();

    if (!result.ok()) {
        discardStaging();
        if (result.error == PackageError::Cancelled) {
            m_pending.reset();
            setState(LauncherUpdateState::Available);
            return;
        }
        LauncherUpdateFailure failure;
        failure.kind = LauncherUpdateFailure::Kind::Package;
        failure.code = static_cast<int>(result.error);
        failure.subcode = static_cast<int>(result.validationProblem);
        failure.detail = result.detail;
        fail(failure);
        return;
    }

    m_pending->manifest = result.manifest;
    qCInfo(lcUpdate).noquote() << "The update to" << m_info.latestVersion << "is ready";
    setState(LauncherUpdateState::Ready);
    if (m_pending->restartWhenReady) {
        if (busy()) {
            // The restart waits for the installations; the user sees why and can restart by hand later.
            m_info.failure = {LauncherUpdateFailure::Kind::Busy, 0, 0, 0, u"projects are being installed"_s};
            emit infoChanged();
        } else {
            apply();
        }
    }
}

// ---- the restart ---------------------------------------------------------------------------------

void LauncherUpdateManager::apply()
{
    if (m_info.state != LauncherUpdateState::Ready || !m_pending)
        return;

    // Not while a project is being installed: the restart would end that.
    if (busy()) {
        m_info.failure = {LauncherUpdateFailure::Kind::Busy, 0, 0, 0, u"projects are being installed"_s};
        emit infoChanged();
        return;
    }
    m_info.failure = {};

    const auto handoffFailure = [this](int code, const QString& detail) {
        LauncherUpdateFailure failure;
        failure.kind = LauncherUpdateFailure::Kind::Handoff;
        failure.code = code;
        failure.detail = detail;
        discardStaging();
        fail(failure);
    };

    // The updater replaces the program file with the name the release has. A launcher
    // that runs under another name would be left behind next to a second copy.
    if (m_runningFile.compare(m_expectations.launcherFile, Qt::CaseInsensitive) != 0) {
        handoffFailure(1,
            u"the launcher runs as \"%1\", not as \"%2\""_s.arg(m_runningFile, m_expectations.launcherFile));
        return;
    }

    // The updater runs from a copy in temp/, so that its own file can be replaced too.
    const QString version = m_pending->selection.release.version.toString();
    const QString jobDir = m_paths.tempDir() + u"/updater-"_s + version;
    QString error;
    if (!FileOps::removeTree(jobDir, &error) || !QDir().mkpath(jobDir)) {
        handoffFailure(2, u"the folder for the updater cannot be prepared"_s);
        return;
    }
    const QString updaterCopy = jobDir + u'/' + m_expectations.updaterFile;
    if (!QFile::copy(m_pending->stagingDir + u'/' + m_pending->manifest.updater, updaterCopy)) {
        handoffFailure(2, u"the updater program cannot be copied"_s);
        return;
    }

    UpdaterJob job;
    job.rootDir = m_paths.root();
    job.stagingDir = m_pending->stagingDir;
    job.fromVersion = m_currentVersion;
    job.toVersion = version;
    job.launcherFile = m_expectations.launcherFile;
    job.updaterFile = m_expectations.updaterFile;
    job.product = m_expectations.product;
    job.launcherProcessId = m_processId;
    const QString jobFile = jobDir + u"/job.json"_s;
    if (!JsonFile::saveAtomic(jobFile, job.toJson(), &error)) {
        handoffFailure(2, u"the job file cannot be written: %1"_s.arg(error));
        return;
    }

    if (!m_startUpdater(updaterCopy, jobFile)) {
        handoffFailure(3, u"the updater program cannot be started"_s);
        return;
    }

    qCInfo(lcUpdate).noquote() << "Handed the update to the updater; the launcher closes now";
    setState(LauncherUpdateState::Applying);
    emit restartRequested();
}

// ---- cancelling and failing -----------------------------------------------------------------------

void LauncherUpdateManager::cancel()
{
    if (!m_pending)
        return;

    switch (m_info.state) {
    case LauncherUpdateState::Downloading:
        if (m_checksumRequest)
            m_checksumRequest->abort();
        if (m_pending->downloadId != 0)
            m_downloads.cancel(m_pending->downloadId);
        m_pending.reset();
        setState(LauncherUpdateState::Available);
        break;
    case LauncherUpdateState::Preparing:
        *m_pending->cancel = true; // reports back through onPrepared()
        break;
    case LauncherUpdateState::Ready:
        discardStaging();
        m_pending.reset();
        setState(LauncherUpdateState::Available);
        break;
    default:
        break; // nothing to cancel, or too late (the updater has started)
    }
}

void LauncherUpdateManager::fail(const LauncherUpdateFailure& failure)
{
    qCWarning(lcUpdate).noquote() << "The launcher update failed:" << static_cast<int>(failure.kind)
                                  << failure.code << failure.httpStatus << failure.detail;
    removeArchive();
    discardStaging();
    m_pending.reset();
    m_info.failure = failure;
    setState(LauncherUpdateState::Failed);
}

void LauncherUpdateManager::discardStaging()
{
    if (m_pending && !m_pending->stagingDir.isEmpty()) {
        QString ignored;
        FileOps::removeTree(m_pending->stagingDir, &ignored);
    }
}

void LauncherUpdateManager::removeArchive()
{
    if (!m_pending || m_pending->archivePath.isEmpty())
        return;
    QFile::remove(m_pending->archivePath);
    QDir().rmdir(QFileInfo(m_pending->archivePath).absolutePath());
    m_pending->archivePath.clear();
}

} // namespace launcher
