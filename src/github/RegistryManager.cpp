#include "github/RegistryManager.h"

#include "core/JsonFile.h"
#include "core/Logger.h"

#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

constexpr auto kCacheSchemaKey = "cache_schema"_L1;
constexpr auto kFetchedAtKey = "fetched_at"_L1;
constexpr auto kRegistryUrlKey = "registry_url"_L1;
constexpr auto kRegistryKey = "registry"_L1;

void logIssues(const RegistryParseResult& parsed)
{
    for (const RegistryIssue& issue : parsed.issues) {
        qCWarning(lcRegistry).noquote()
            << "Rejected registry entry" << issue.index
            << (issue.projectId.isEmpty() ? QString() : u'(' + issue.projectId + u')') << '-'
            << issue.message;
    }
}

} // namespace

RegistryManager::RegistryManager(
    GitHubClient& client, const QString& cacheFile, const QUrl& registryUrl, QObject* parent)
    : QObject(parent)
    , m_client(client)
    , m_cacheFile(cacheFile)
    , m_registryUrl(registryUrl)
{
    m_options.maxBytes = kMaxRegistryBytes;
    m_autoTimer.setSingleShot(false);
    connect(&m_autoTimer, &QTimer::timeout, this, &RegistryManager::refresh);
}

RegistryManager::~RegistryManager()
{
    if (m_request)
        m_request->abort();
}

void RegistryManager::loadCache()
{
    const JsonFile::LoadResult file = JsonFile::load(m_cacheFile);
    if (file.status == JsonFile::Status::NotFound)
        return;
    if (file.status != JsonFile::Status::Ok) {
        qCWarning(lcRegistry).noquote() << "Registry cache is unreadable:" << file.error;
        if (file.status == JsonFile::Status::Corrupt)
            JsonFile::backupAside(m_cacheFile, u"corrupt"_s, JsonFile::BackupMode::Move);
        return;
    }

    const QJsonObject cache = file.object;
    if (cache.value(kCacheSchemaKey).toInt(-1) != kCacheSchemaVersion) {
        qCInfo(lcRegistry) << "Registry cache has an unknown layout and is ignored";
        return;
    }
    // A list that came from another address (after a rebranding, say) is not
    // vouched for by this launcher's registry.
    if (cache.value(kRegistryUrlKey).toString() != m_registryUrl.toString()) {
        qCInfo(lcRegistry) << "Registry cache belongs to another address and is ignored";
        return;
    }
    const QDateTime fetchedAt = QDateTime::fromString(cache.value(kFetchedAtKey).toString(), Qt::ISODate);
    if (!fetchedAt.isValid() || !cache.value(kRegistryKey).isObject()) {
        qCWarning(lcRegistry) << "Registry cache is incomplete and is ignored";
        return;
    }

    // The cache is data from disk, not a trusted result: it is validated again
    // with the rules of this version.
    const RegistryParseResult parsed = RegistryParser::parse(cache.value(kRegistryKey).toObject());
    if (!parsed.ok()) {
        qCWarning(lcRegistry).noquote() << "Registry cache is not usable:" << parsed.error;
        return;
    }

    logIssues(parsed);
    adopt(parsed, Source::Cache, fetchedAt);
    qCInfo(lcRegistry).noquote() << "Registry loaded from cache:" << m_projects.size() << "projects, fetched"
                                 << fetchedAt.toString(Qt::ISODate);
    emit changed();
}

void RegistryManager::refresh()
{
    if (m_request)
        return;

    qCInfo(lcRegistry) << "Loading registry";
    m_loading = true;
    m_request = m_client.get(m_registryUrl, m_options);
    connect(m_request, &GitHubRequest::finished, this, &RegistryManager::onFetched);
    emit changed();
}

void RegistryManager::setAutoRefresh(bool enabled, int intervalMinutes)
{
    m_autoMinutes = intervalMinutes;
    if (!enabled || intervalMinutes <= 0) {
        m_autoTimer.stop();
        return;
    }
    // Restarting is deliberate: a changed interval counts from now.
    m_autoTimer.start(intervalMinutes * 60 * 1000);
}

void RegistryManager::onFetched(const NetworkResult& result)
{
    m_loading = false;
    m_request.clear();

    if (!result.ok()) {
        switch (result.error) {
        case NetworkError::RateLimited:
            fail(Problem::RateLimited, result.httpStatus);
            break;
        case NetworkError::HttpError:
            fail(Problem::Http, result.httpStatus);
            break;
        case NetworkError::Network:
        case NetworkError::Timeout:
            fail(Problem::Unreachable, 0);
            break;
        default: // too large, redirect problems, address not allowed, aborted
            fail(Problem::InvalidContent, result.httpStatus);
            break;
        }
        return;
    }

    const RegistryParseResult parsed = RegistryParser::parse(result.body);
    if (!parsed.ok()) {
        qCWarning(lcRegistry).noquote() << "Registry answer is not usable:" << parsed.error;
        fail(parsed.status == RegistryParseResult::Status::UnsupportedVersion ? Problem::UnsupportedVersion
                                                                              : Problem::InvalidContent,
            result.httpStatus);
        return;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    logIssues(parsed);
    adopt(parsed, Source::Network, now);
    writeCache(parsed, now);
    m_problem = Problem::None;
    m_httpStatus = 0;

    qCInfo(lcRegistry) << "Registry loaded:" << m_projects.size() << "projects," << parsed.disabledCount
                       << "disabled," << parsed.issues.size() << "rejected";
    emit changed();
    emit refreshFinished(true);
}

void RegistryManager::adopt(const RegistryParseResult& parsed, Source source, const QDateTime& fetchedAt)
{
    m_projects = parsed.projects;
    m_issues = parsed.issues;
    m_registryName = parsed.registryName;
    m_source = source;
    m_fetchedAt = fetchedAt;
}

void RegistryManager::writeCache(const RegistryParseResult& parsed, const QDateTime& fetchedAt)
{
    QJsonObject cache;
    cache.insert(kCacheSchemaKey, kCacheSchemaVersion);
    cache.insert(kFetchedAtKey, fetchedAt.toUTC().toString(Qt::ISODate));
    cache.insert(kRegistryUrlKey, m_registryUrl.toString());
    cache.insert(kRegistryKey, parsed.document);

    QString error;
    if (!JsonFile::saveAtomic(m_cacheFile, cache, &error))
        qCWarning(lcRegistry).noquote() << "Registry cache could not be saved:" << error;
}

void RegistryManager::fail(Problem problem, int httpStatus)
{
    // The list that is already there stays: it was valid when it arrived.
    m_problem = problem;
    m_httpStatus = httpStatus;
    qCWarning(lcRegistry) << "Registry refresh failed, keeping"
                          << (m_source == Source::None ? "no list" : "the previous list");
    emit changed();
    emit refreshFinished(false);
}

} // namespace launcher
