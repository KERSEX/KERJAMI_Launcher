#pragma once

#include <QDateTime>
#include <QString>
#include <QUrl>

namespace launcher {

using DownloadId = quint64;

enum class DownloadState {
    Queued,
    Running,
    Finished,
    Failed,
    Cancelled,
};

/// Why a download failed.
enum class DownloadProblem {
    None,
    InvalidRequest,    ///< The request did not pass the checks, or a redirect left the allowed hosts.
    InsufficientSpace, ///< Not enough free space on the drive.
    StorageError,      ///< The file could not be written or moved into place.
    Unreachable,       ///< No connection or timeout.
    RateLimited,       ///< The server's request limit is used up.
    Http,              ///< The server answered with an error status, see Download::httpStatus.
    SizeMismatch,      ///< The file has not the size the release announced.
    TooLarge,          ///< The server sends more than the release announced.
};

struct DownloadRequest {
    QString projectId; ///< Becomes the sub-folder of the downloads folder; must be a valid project id.
    QString title;     ///< Shown to the user, for example "KERS Overlay 1.2.0".
    QUrl url;
    QString fileName;        ///< A plain, safe file name.
    qint64 expectedSize = 0; ///< Size of the asset according to the release. Required.
};

struct Download {
    DownloadId id = 0;
    DownloadRequest request;

    DownloadState state = DownloadState::Queued;
    DownloadProblem problem = DownloadProblem::None;
    int httpStatus = 0;
    QString detail; ///< Technical detail for the log, not for the user.

    qint64 bytesReceived = 0;
    qint64 bytesTotal = 0;
    qint64 bytesPerSecond = 0;

    QString filePath; ///< The finished file; empty until then.
    QDateTime enqueuedAt;
    QDateTime finishedAt;

    bool isActive() const { return state == DownloadState::Queued || state == DownloadState::Running; }

    /// Seconds until the download is complete at the current speed, -1 if unknown.
    qint64 remainingSeconds() const
    {
        if (state != DownloadState::Running || bytesPerSecond <= 0 || bytesTotal <= bytesReceived)
            return -1;
        return (bytesTotal - bytesReceived + bytesPerSecond - 1) / bytesPerSecond;
    }
};

} // namespace launcher
