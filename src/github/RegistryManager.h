#pragma once

#include "github/GitHubClient.h"
#include "github/RegistryParser.h"
#include "models/Project.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

namespace launcher {

/// Holds the list of approved projects.
///
/// The list comes from the registry on GitHub. Every valid answer is cached on
/// disk, and when GitHub cannot be reached the manager keeps working with the
/// last valid list: from the cache after a start without a connection, from
/// memory when a refresh fails later. A broken or too new registry never
/// replaces a good list.
class RegistryManager : public QObject {
    Q_OBJECT

public:
    /// Where the current list comes from.
    enum class Source {
        None,    ///< Nothing loaded yet.
        Network, ///< The last refresh succeeded.
        Cache,   ///< Loaded from disk; the last refresh did not happen or failed.
    };

    /// Why the last refresh failed. None after a successful one.
    enum class Problem {
        None,
        Unreachable,        ///< No connection or timeout.
        RateLimited,        ///< GitHub's request limit is used up.
        Http,               ///< The server answered with an error status, see httpStatus().
        InvalidContent,     ///< The answer is not a usable registry.
        UnsupportedVersion, ///< The registry needs a newer launcher.
    };

    /// Version of the cache file layout.
    static constexpr int kCacheSchemaVersion = 1;
    static constexpr qint64 kMaxRegistryBytes = 1024 * 1024;

    RegistryManager(
        GitHubClient& client, const QString& cacheFile, const QUrl& registryUrl, QObject* parent = nullptr);
    ~RegistryManager() override;

    /// Timeouts and retries of the registry request. The defaults suit production.
    void setFetchOptions(const FetchOptions& options) { m_options = options; }

    /// Reads the cache file, if there is a valid one. Call once at start so that
    /// the list is available before (and without) a network answer.
    void loadCache();

    /// Fetches the registry. Does nothing while a fetch is running.
    void refresh();

    /// Fetches the registry now and then every @p intervalMinutes while @p enabled.
    /// Does not fetch immediately.
    void setAutoRefresh(bool enabled, int intervalMinutes);
    bool autoRefreshActive() const { return m_autoTimer.isActive(); }
    int autoRefreshIntervalMinutes() const { return m_autoMinutes; }

    /// Enabled, valid projects in registry order.
    const QList<Project>& projects() const { return m_projects; }
    QString registryName() const { return m_registryName; }

    Source source() const { return m_source; }
    QDateTime fetchedAt() const { return m_fetchedAt; } ///< When the list was fetched; invalid if none.

    Problem problem() const { return m_problem; }
    int httpStatus() const { return m_httpStatus; } ///< Status of the failed answer, 0 if there was none.
    bool isLoading() const { return m_loading; }

    /// Entries of the current list's document that were rejected.
    const QList<RegistryIssue>& issues() const { return m_issues; }

signals:
    /// Anything shown to the user changed: the list, its source, the problem or the loading state.
    void changed();

    /// A refresh has ended.
    void refreshFinished(bool success);

private:
    void onFetched(const NetworkResult& result);
    void adopt(const RegistryParseResult& parsed, Source source, const QDateTime& fetchedAt);
    void writeCache(const RegistryParseResult& parsed, const QDateTime& fetchedAt);
    void fail(Problem problem, int httpStatus);

    GitHubClient& m_client;
    QString m_cacheFile;
    QUrl m_registryUrl;
    FetchOptions m_options;

    QPointer<GitHubRequest> m_request;
    QTimer m_autoTimer;
    int m_autoMinutes = 0;

    QList<Project> m_projects;
    QList<RegistryIssue> m_issues;
    QString m_registryName;
    QDateTime m_fetchedAt;
    Source m_source = Source::None;
    Problem m_problem = Problem::None;
    int m_httpStatus = 0;
    bool m_loading = false;
};

} // namespace launcher
