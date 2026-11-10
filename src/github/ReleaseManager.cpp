#include "github/ReleaseManager.h"

#include "core/Logger.h"
#include "github/RegistryParser.h"
#include "github/ReleaseParser.h"

#include <QTimer>

using namespace Qt::StringLiterals;

namespace launcher {

ReleaseEndpoints ReleaseEndpoints::production()
{
    return {QUrl(u"https://api.github.com"_s), QUrl(u"https://github.com"_s)};
}

ReleaseManager::ReleaseManager(GitHubClient& client, QObject* parent)
    : ReleaseManager(client, ReleaseEndpoints::production(), parent)
{
}

ReleaseManager::ReleaseManager(GitHubClient& client, const ReleaseEndpoints& endpoints, QObject* parent)
    : QObject(parent)
    , m_client(client)
    , m_endpoints(endpoints)
{
    m_options.accept = "application/vnd.github+json";
}

ReleaseManager::~ReleaseManager()
{
    for (const QPointer<GitHubRequest>& request : std::as_const(m_running)) {
        if (request)
            request->abort();
    }
}

bool ReleaseManager::isFetching(const QString& projectId) const
{
    return !m_running.value(projectId).isNull();
}

std::optional<ReleaseSelection> ReleaseManager::latest(const QString& projectId) const
{
    const auto found = m_latest.constFind(projectId);
    if (found == m_latest.cend())
        return std::nullopt;
    return *found;
}

void ReleaseManager::fetchLatest(const Project& project)
{
    if (isFetching(project.id))
        return;

    // The address is built from these values, so they are checked again here,
    // however the project came to be.
    const bool valid = RegistryParser::isValidProjectId(project.id)
        && RegistryParser::isValidOwner(project.owner)
        && RegistryParser::isValidRepository(project.repository)
        && RegistryParser::isValidAssetPattern(project.assetPattern);
    if (!valid) {
        qCWarning(lcReleases) << "Not fetching releases: the project data is not valid";
        QTimer::singleShot(0, this, [this, project] { fail(project, Problem::InvalidProject, 0); });
        return;
    }

    QUrl url = m_endpoints.api;
    url.setPath(u"/repos/%1/%2/releases"_s.arg(project.owner, project.repository));
    url.setQuery(u"per_page=30"_s);

    qCInfo(lcReleases).noquote() << "Looking for the newest release of" << project.id;
    GitHubRequest* request = m_client.get(url, m_options);
    m_running.insert(project.id, request);
    connect(request, &GitHubRequest::finished, this,
        [this, project](const NetworkResult& result) { onFetched(project, result); });
}

void ReleaseManager::onFetched(const Project& project, const NetworkResult& result)
{
    m_running.remove(project.id);

    if (!result.ok()) {
        switch (result.error) {
        case NetworkError::RateLimited:
            fail(project, Problem::RateLimited, result.httpStatus);
            break;
        case NetworkError::HttpError:
            fail(project, Problem::Http, result.httpStatus);
            break;
        case NetworkError::Network:
        case NetworkError::Timeout:
            fail(project, Problem::Unreachable, 0);
            break;
        default: // too large, redirect problems, address not allowed, aborted
            fail(project, Problem::InvalidContent, result.httpStatus);
            break;
        }
        return;
    }

    const ReleaseListResult list = ReleaseParser::parseList(result.body);
    if (!list.ok) {
        qCWarning(lcReleases).noquote() << "Release answer is not usable:" << list.error;
        fail(project, Problem::InvalidContent, result.httpStatus);
        return;
    }

    // The newest release decides. If it lacks the file, an older release is not
    // offered instead: that would look like an update and install an old version.
    const Release* newest = ReleaseParser::latest(list.releases);
    if (!newest) {
        qCInfo(lcReleases) << "No stable release for" << project.id << '(' << list.skipped
                           << "entries skipped )";
        fail(project, Problem::NoRelease, 0);
        return;
    }

    const QList<ReleaseAsset> matches = ReleaseParser::matchingAssets(*newest, project.assetPattern);
    if (matches.isEmpty()) {
        qCWarning(lcReleases).noquote() << "Release" << newest->tag << "of" << project.id
                                        << "has no file matching" << project.assetPattern;
        fail(project, Problem::NoMatchingAsset, 0);
        return;
    }
    if (matches.size() > 1) {
        QStringList names;
        for (const ReleaseAsset& asset : matches)
            names.append(asset.name);
        qCWarning(lcReleases).noquote()
            << "Release" << newest->tag << "of" << project.id << "has several files matching"
            << project.assetPattern << ':' << names.join(u", "_s);
        fail(project, Problem::AmbiguousAsset, 0);
        return;
    }

    const ReleaseAsset& asset = matches.first();
    ReleaseSelection selection;
    selection.release = *newest;
    selection.asset = asset;
    selection.downloadUrl = downloadUrl(project, *newest, asset);
    m_latest.insert(project.id, selection);

    qCInfo(lcReleases).noquote() << "Newest release of" << project.id << "is" << newest->version.toString()
                                 << '(' << asset.name << ',' << asset.size << "bytes,"
                                 << (asset.sha256.isEmpty() ? "no digest" : "with digest") << ')';
    emit releaseReady(project.id, selection);
}

void ReleaseManager::fail(const Project& project, Problem problem, int httpStatus)
{
    emit fetchFailed(project.id, problem, httpStatus);
}

QUrl ReleaseManager::downloadUrl(
    const Project& project, const Release& release, const ReleaseAsset& asset) const
{
    // Built from checked parts, not taken from the answer: owner and repository
    // come from the registry, the tag is a version, the name passed the file name rules.
    QUrl url = m_endpoints.downloads;
    url.setPath(
        u"/%1/%2/releases/download/%3/%4"_s.arg(project.owner, project.repository, release.tag, asset.name));
    return url;
}

} // namespace launcher
