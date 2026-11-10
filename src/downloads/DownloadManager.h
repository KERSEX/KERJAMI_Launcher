#pragma once

#include "downloads/TransferRate.h"
#include "github/GitHubClient.h"
#include "models/Download.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>

#include <functional>
#include <optional>

namespace launcher {

/// Downloads release files into the portable folder.
///
/// A queue runs a few downloads at a time. Each one is written to a partial
/// file in the temp folder and moved to downloads/<project>/<file> only after
/// it has arrived completely and has exactly the announced size. A download
/// that fails or is cancelled leaves no file behind. Whether the file is the
/// right one is decided afterwards by the integrity check (SHA-256).
class DownloadManager : public QObject {
    Q_OBJECT

public:
    static constexpr int kDefaultParallel = 2;

    /// Finished, failed and cancelled entries kept for the list; older ones are dropped.
    static constexpr int kMaxKeptFinished = 100;

    /// Free space that must remain on top of the file itself.
    static constexpr qint64 kSpaceReserve = 16LL * 1024 * 1024;

    DownloadManager(
        GitHubClient& client, const QString& tempDir, const QString& downloadsDir, QObject* parent = nullptr);
    ~DownloadManager() override;

    /// Number of downloads running at the same time, at least 1.
    void setMaxParallel(int count);

    /// Timeouts and retries of the transfers. The defaults suit production.
    void setFetchOptions(const FetchOptions& options) { m_options = options; }

    /// How the free space of a folder's drive is determined, in bytes (-1 if
    /// unknown, which skips the check). Replaced only by tests.
    void setAvailableSpaceFunction(std::function<qint64(const QString&)> function)
    {
        m_availableSpace = std::move(function);
    }

    /// Adds a download to the queue and returns its id. A request that does
    /// not pass the checks becomes a failed entry (DownloadProblem::InvalidRequest).
    DownloadId enqueue(const DownloadRequest& request);

    /// Stops a queued or running download and removes its partial file.
    void cancel(DownloadId id);

    /// Forgets all finished, failed and cancelled entries. Files stay on disk.
    void clearFinished();

    /// All entries in the order they were added.
    QList<Download> downloads() const;
    std::optional<Download> download(DownloadId id) const;

    /// Queued and running downloads.
    int activeCount() const;

signals:
    void added(quint64 id);

    /// The state of an entry changed (started, finished, failed, cancelled).
    void stateChanged(quint64 id);

    /// Bytes or speed changed. Emitted at most about ten times a second per download.
    void progressChanged(quint64 id);

    /// A download completed; its file is in Download::filePath.
    void finished(quint64 id);

    void failed(quint64 id);

    /// An old entry was dropped from the list.
    void removed(quint64 id);

private:
    struct Entry {
        Download download;
        QPointer<GitHubRequest> request;
        TransferRate rate;
        QElapsedTimer clock;
        qint64 lastEmitMs = -1000;
    };

    QString validate(const DownloadRequest& request) const;
    void startQueued();
    void start(DownloadId id);
    void onProgress(DownloadId id, qint64 received, qint64 total);
    void onFinished(DownloadId id, const NetworkResult& result);
    void complete(DownloadId id);
    void finish(
        DownloadId id, DownloadState state, DownloadProblem problem, int httpStatus, const QString& detail);
    void trimHistory();
    QString partialFile(DownloadId id) const;

    GitHubClient& m_client;
    QString m_tempDir;
    QString m_downloadsDir;
    FetchOptions m_options;
    std::function<qint64(const QString&)> m_availableSpace;
    int m_maxParallel = kDefaultParallel;
    DownloadId m_lastId = 0;
    QHash<DownloadId, Entry> m_entries;
    QList<DownloadId> m_order;
};

} // namespace launcher
