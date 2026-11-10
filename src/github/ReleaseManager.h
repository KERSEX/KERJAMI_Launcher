#pragma once

#include "github/GitHubClient.h"
#include "models/Project.h"
#include "models/Release.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUrl>

#include <optional>

namespace launcher {

/// Where releases and their files are fetched from.
///
/// Application code always uses production(). Tests point both at a server on
/// the local machine; no setting or file can change them in a running launcher.
struct ReleaseEndpoints {
    QUrl api;       ///< Answers "list releases", https://api.github.com
    QUrl downloads; ///< Serves the files, https://github.com

    static ReleaseEndpoints production();
};

/// Finds the newest stable release of a project and the asset to install.
///
/// One request to the GitHub API per project. The API allows 60 requests per
/// hour without login, so callers fetch on demand (opening a project, checking
/// installed projects for updates) and never for the whole registry at once.
class ReleaseManager : public QObject {
    Q_OBJECT

public:
    /// Why no release could be selected.
    enum class Problem {
        None,
        InvalidProject,  ///< The project's owner, repository, id or asset pattern is not valid.
        Unreachable,     ///< No connection or timeout.
        RateLimited,     ///< GitHub's request limit is used up.
        Http,            ///< The server answered with an error status, see httpStatus.
        InvalidContent,  ///< The answer is not a list of releases.
        NoRelease,       ///< There is no stable release.
        NoMatchingAsset, ///< The newest stable release has no file that fits the project.
        AmbiguousAsset,  ///< More than one file fits; none is chosen.
    };

    explicit ReleaseManager(GitHubClient& client, QObject* parent = nullptr);
    ReleaseManager(GitHubClient& client, const ReleaseEndpoints& endpoints, QObject* parent = nullptr);
    ~ReleaseManager() override;

    void setFetchOptions(const FetchOptions& options) { m_options = options; }

    /// Asks GitHub for the project's releases. The result arrives through
    /// releaseReady() or fetchFailed(). Does nothing while a request for the
    /// same project is running.
    void fetchLatest(const Project& project);

    bool isFetching(const QString& projectId) const;

    /// The last successful result for a project. A later failed fetch does not clear it.
    std::optional<ReleaseSelection> latest(const QString& projectId) const;

signals:
    void releaseReady(const QString& projectId, const launcher::ReleaseSelection& selection);
    void fetchFailed(const QString& projectId, launcher::ReleaseManager::Problem problem, int httpStatus);

private:
    void onFetched(const Project& project, const NetworkResult& result);
    void fail(const Project& project, Problem problem, int httpStatus);
    QUrl downloadUrl(const Project& project, const Release& release, const ReleaseAsset& asset) const;

    GitHubClient& m_client;
    ReleaseEndpoints m_endpoints;
    FetchOptions m_options;
    QHash<QString, QPointer<GitHubRequest>> m_running;
    QHash<QString, ReleaseSelection> m_latest;
};

} // namespace launcher
