#include "downloads/DownloadManager.h"

#include "core/Logger.h"
#include "core/PathManager.h"
#include "github/RegistryParser.h"
#include "github/ReleaseParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

constexpr qint64 kProgressIntervalMs = 100;

DownloadProblem problemFor(NetworkError error)
{
    switch (error) {
    case NetworkError::RateLimited:
        return DownloadProblem::RateLimited;
    case NetworkError::HttpError:
        return DownloadProblem::Http;
    case NetworkError::Network:
    case NetworkError::Timeout:
        return DownloadProblem::Unreachable;
    case NetworkError::TooLarge:
        return DownloadProblem::TooLarge;
    case NetworkError::FileError:
        return DownloadProblem::StorageError;
    case NetworkError::None:
    case NetworkError::UrlNotAllowed:
    case NetworkError::TooManyRedirects:
    case NetworkError::Aborted:
        break;
    }
    return DownloadProblem::InvalidRequest;
}

} // namespace

DownloadManager::DownloadManager(
    GitHubClient& client, const QString& tempDir, const QString& downloadsDir, QObject* parent)
    : QObject(parent)
    , m_client(client)
    , m_tempDir(QDir::cleanPath(tempDir))
    , m_downloadsDir(QDir::cleanPath(downloadsDir))
{
    m_options.accept = "application/octet-stream";
    m_options.timeoutMs = 30000;
    // Release files are large; the limit is set per download from the announced size.
    m_options.maxBytes = ReleaseParser::kMaxAssetBytes;

    // Partial files of an earlier run that ended abruptly. This launcher is the
    // only one using the folder, so nothing here can belong to a running download.
    const QStringList leftovers = QDir(m_tempDir).entryList({u"download-*.part"_s}, QDir::Files);
    for (const QString& name : leftovers) {
        if (QFile::remove(m_tempDir + u'/' + name))
            qCInfo(lcDownload).noquote() << "Removed leftover partial file" << name;
    }
}

DownloadManager::~DownloadManager()
{
    // Stops running transfers; their partial files are removed on the way.
    for (const DownloadId id : std::as_const(m_order)) {
        Entry& entry = m_entries[id];
        if (entry.request) {
            entry.download.state = DownloadState::Cancelled;
            entry.request->abort();
        }
    }
}

void DownloadManager::setMaxParallel(int count)
{
    m_maxParallel = std::max(1, count);
    startQueued();
}

QList<Download> DownloadManager::downloads() const
{
    QList<Download> result;
    result.reserve(m_order.size());
    for (const DownloadId id : m_order)
        result.append(m_entries.value(id).download);
    return result;
}

std::optional<Download> DownloadManager::download(DownloadId id) const
{
    const auto found = m_entries.constFind(id);
    if (found == m_entries.cend())
        return std::nullopt;
    return found->download;
}

int DownloadManager::activeCount() const
{
    return static_cast<int>(std::count_if(
        m_entries.cbegin(), m_entries.cend(), [](const Entry& entry) { return entry.download.isActive(); }));
}

QString DownloadManager::validate(const DownloadRequest& request) const
{
    if (!RegistryParser::isValidProjectId(request.projectId))
        return u"the project id is not valid"_s;
    if (!ReleaseParser::isValidAssetFileName(request.fileName))
        return u"the file name is not valid"_s;
    if (request.expectedSize < 1 || request.expectedSize > ReleaseParser::kMaxAssetBytes)
        return u"the announced size is not plausible"_s;
    if (!m_client.policy().allows(request.url))
        return u"the address is not on the allow list"_s;
    return {};
}

DownloadId DownloadManager::enqueue(const DownloadRequest& request)
{
    // The same file for the same project twice would race for one target.
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        const DownloadRequest& other = it->download.request;
        if (it->download.isActive() && other.projectId == request.projectId
            && other.fileName == request.fileName) {
            qCInfo(lcDownload) << "Download of" << request.fileName << "is already under way as"
                               << it->download.id;
            return it->download.id;
        }
    }

    const DownloadId id = ++m_lastId;

    Entry entry;
    entry.download.id = id;
    entry.download.request = request;
    if (entry.download.request.title.isEmpty())
        entry.download.request.title = request.fileName;
    entry.download.bytesTotal = request.expectedSize;
    entry.download.enqueuedAt = QDateTime::currentDateTimeUtc();
    m_entries.insert(id, entry);
    m_order.append(id);
    emit added(id);

    const QString problem = validate(request);
    if (!problem.isEmpty()) {
        qCWarning(lcDownload).noquote() << "Download rejected:" << problem;
        finish(id, DownloadState::Failed, DownloadProblem::InvalidRequest, 0, problem);
        return id;
    }

    qCInfo(lcDownload).noquote() << "Download" << id << "queued:" << request.projectId << request.fileName
                                 << request.expectedSize << "bytes";
    startQueued();
    return id;
}

void DownloadManager::startQueued()
{
    int running = static_cast<int>(std::count_if(m_entries.cbegin(), m_entries.cend(),
        [](const Entry& entry) { return entry.download.state == DownloadState::Running; }));

    // Copy: finishing an entry can change the list while it is being walked.
    const QList<DownloadId> order = m_order;
    for (const DownloadId id : order) {
        if (running >= m_maxParallel)
            break;
        if (m_entries.value(id).download.state == DownloadState::Queued) {
            start(id);
            if (m_entries.value(id).download.state == DownloadState::Running)
                ++running;
        }
    }
}

QString DownloadManager::partialFile(DownloadId id) const
{
    return u"%1/download-%2.part"_s.arg(m_tempDir).arg(id);
}

void DownloadManager::start(DownloadId id)
{
    Entry& entry = m_entries[id];
    const DownloadRequest& request = entry.download.request;

    if (!QDir().mkpath(m_tempDir) || !QDir().mkpath(m_downloadsDir + u'/' + request.projectId)) {
        finish(id, DownloadState::Failed, DownloadProblem::StorageError, 0,
            u"cannot create the download folders"_s);
        return;
    }

    // Better to refuse at the start than to fail after hours with a full drive.
    const qint64 available =
        m_availableSpace ? m_availableSpace(m_tempDir) : QStorageInfo(m_tempDir).bytesAvailable();
    if (available >= 0 && available < request.expectedSize + kSpaceReserve) {
        finish(id, DownloadState::Failed, DownloadProblem::InsufficientSpace, 0,
            u"%1 bytes free, %2 needed"_s.arg(available).arg(request.expectedSize + kSpaceReserve));
        return;
    }

    FetchOptions options = m_options;
    options.saveToFile = partialFile(id);
    // Anything beyond the announced size is wrong, however much the server wants to send.
    options.maxBytes = request.expectedSize;

    entry.download.state = DownloadState::Running;
    entry.rate.reset();
    entry.clock.start();
    entry.lastEmitMs = -kProgressIntervalMs;
    entry.request = m_client.get(request.url, options);
    connect(entry.request, &GitHubRequest::downloadProgress, this,
        [this, id](qint64 received, qint64 total) { onProgress(id, received, total); });
    connect(entry.request, &GitHubRequest::finished, this,
        [this, id](const NetworkResult& result) { onFinished(id, result); });

    qCInfo(lcDownload) << "Download" << id << "started";
    emit stateChanged(id);
}

void DownloadManager::onProgress(DownloadId id, qint64 received, qint64 total)
{
    const auto found = m_entries.find(id);
    if (found == m_entries.end() || found->download.state != DownloadState::Running)
        return;
    Entry& entry = *found;

    entry.download.bytesReceived = received;
    if (total > 0)
        entry.download.bytesTotal = total;

    const qint64 now = entry.clock.elapsed();
    entry.rate.addSample(now, received);
    entry.download.bytesPerSecond = entry.rate.bytesPerSecond();

    const bool done = entry.download.bytesTotal > 0 && received >= entry.download.bytesTotal;
    if (done || now - entry.lastEmitMs >= kProgressIntervalMs) {
        entry.lastEmitMs = now;
        emit progressChanged(id);
    }
}

void DownloadManager::onFinished(DownloadId id, const NetworkResult& result)
{
    const auto found = m_entries.find(id);
    if (found == m_entries.end())
        return;
    Entry& entry = *found;
    entry.request.clear();

    // Cancelled by the user (or by shutting down): nothing to report but the file to remove.
    if (entry.download.state == DownloadState::Cancelled) {
        QFile::remove(partialFile(id));
        qCInfo(lcDownload) << "Download" << id << "cancelled";
        return;
    }

    if (!result.ok()) {
        QFile::remove(partialFile(id));
        finish(id, DownloadState::Failed, problemFor(result.error), result.httpStatus, result.detail);
        startQueued();
        return;
    }

    entry.download.bytesReceived = result.bytesReceived;
    complete(id);
    startQueued();
}

/// The data has arrived: check its size and move it to its place.
void DownloadManager::complete(DownloadId id)
{
    Entry& entry = m_entries[id];
    const DownloadRequest& request = entry.download.request;
    const QString partial = partialFile(id);

    const qint64 size = QFileInfo(partial).size();
    if (size != request.expectedSize) {
        QFile::remove(partial);
        finish(id, DownloadState::Failed, DownloadProblem::SizeMismatch, 0,
            u"received %1 bytes, expected %2"_s.arg(size).arg(request.expectedSize));
        return;
    }

    const QString target = u"%1/%2/%3"_s.arg(m_downloadsDir, request.projectId, request.fileName);
    // The names are checked already; this is the second lock on the same door.
    if (!PathManager::isInside(m_downloadsDir, target)) {
        QFile::remove(partial);
        finish(id, DownloadState::Failed, DownloadProblem::InvalidRequest, 0,
            u"the target is outside the downloads folder"_s);
        return;
    }
    if (QFileInfo::exists(target) && !QFile::remove(target)) {
        QFile::remove(partial);
        finish(id, DownloadState::Failed, DownloadProblem::StorageError, 0,
            u"an earlier file cannot be replaced"_s);
        return;
    }
    if (!QFile::rename(partial, target)) {
        QFile::remove(partial);
        finish(id, DownloadState::Failed, DownloadProblem::StorageError, 0,
            u"the file cannot be moved into place"_s);
        return;
    }

    entry.download.filePath = target;
    entry.download.bytesReceived = size;
    finish(id, DownloadState::Finished, DownloadProblem::None, 0, {});
}

void DownloadManager::cancel(DownloadId id)
{
    const auto found = m_entries.find(id);
    if (found == m_entries.end() || !found->download.isActive())
        return;
    Entry& entry = *found;

    const bool wasRunning = entry.download.state == DownloadState::Running;
    entry.download.state = DownloadState::Cancelled;
    entry.download.finishedAt = QDateTime::currentDateTimeUtc();
    entry.download.bytesPerSecond = 0;
    if (wasRunning && entry.request)
        entry.request->abort(); // reports back through onFinished(), which removes the partial file

    emit stateChanged(id);
    trimHistory();
    startQueued();
}

void DownloadManager::finish(
    DownloadId id, DownloadState state, DownloadProblem problem, int httpStatus, const QString& detail)
{
    Entry& entry = m_entries[id];
    entry.download.state = state;
    entry.download.problem = problem;
    entry.download.httpStatus = httpStatus;
    entry.download.detail = detail;
    entry.download.bytesPerSecond = 0;
    entry.download.finishedAt = QDateTime::currentDateTimeUtc();

    if (state == DownloadState::Finished) {
        qCInfo(lcDownload).noquote() << "Download" << id << "finished:" << entry.download.request.fileName;
        emit stateChanged(id);
        emit finished(id);
    } else {
        qCWarning(lcDownload).noquote() << "Download" << id << "failed:" << detail;
        emit stateChanged(id);
        emit failed(id);
    }
    trimHistory();
}

void DownloadManager::clearFinished()
{
    const QList<DownloadId> order = m_order;
    for (const DownloadId id : order) {
        if (m_entries.value(id).download.isActive())
            continue;
        m_entries.remove(id);
        m_order.removeOne(id);
        emit removed(id);
    }
}

void DownloadManager::trimHistory()
{
    int kept = static_cast<int>(std::count_if(
        m_entries.cbegin(), m_entries.cend(), [](const Entry& entry) { return !entry.download.isActive(); }));

    const QList<DownloadId> order = m_order;
    for (const DownloadId id : order) {
        if (kept <= kMaxKeptFinished)
            break;
        if (!m_entries.value(id).download.isActive()) {
            m_entries.remove(id);
            m_order.removeOne(id);
            --kept;
            emit removed(id);
        }
    }
}

} // namespace launcher
