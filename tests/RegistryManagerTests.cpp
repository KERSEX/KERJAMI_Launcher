// RegistryManager: loading, the on-disk cache and what happens when GitHub is
// not reachable or answers with something unusable. The registry is served by
// a server on the local machine.

#include "TestHttpServer.h"
#include "core/JsonFile.h"
#include "github/GitHubClient.h"
#include "github/RegistryManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using launcher::FetchOptions;
using launcher::GitHubClient;
using launcher::RegistryManager;
using Problem = launcher::RegistryManager::Problem;
using Source = launcher::RegistryManager::Source;
using Response = TestHttpServer::Response;
using Request = TestHttpServer::Request;

namespace {

QByteArray validRegistry()
{
    return R"({
        "schema_version": 1,
        "registry_name": "Test Registry",
        "projects": [
            {"id": "alpha", "name": "Alpha", "description": "First project", "owner": "KERSEX",
             "repository": "Alpha", "category": "Tools", "tags": ["A", "B"], "enabled": true, "featured": true,
             "asset_pattern": "Alpha-*.zip", "executable": "Alpha.exe", "install_directory": "Alpha"},
            {"id": "beta", "name": "Beta", "description": "Second project", "owner": "KERSEX",
             "repository": "Beta", "category": "Tools", "enabled": true,
             "asset_pattern": "Beta.zip", "executable": "bin/Beta.exe", "install_directory": "Beta"}
        ]
    })";
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

Response body(const QByteArray& content, int status = 200)
{
    Response response;
    response.status = status;
    response.body = content;
    return response;
}

} // namespace

class RegistryManagerTests : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void refreshLoadsTheListAndWritesTheCache();
    void changedIsReportedForLoadingAndForTheResult();
    void cacheIsAvailableBeforeAnyNetworkAnswer();
    void refreshWhileLoadingIsIgnored();

    void noConnectionWithoutCache();
    void noConnectionKeepsTheCachedList();
    void httpErrorKeepsTheCachedList();
    void rateLimitIsReported();
    void brokenAnswerKeepsTheCachedList();
    void tooNewRegistryKeepsTheCachedList();
    void failedRefreshAfterSuccessKeepsTheListInMemory();
    void recoveryClearsTheProblem();

    void invalidEntriesAreDroppedAndTheRestIsKept();
    void answerWithoutAnyValidProjectReplacesTheList();

    void corruptCacheFileIsMovedAside();
    void cacheOfAnotherAddressIsIgnored();
    void cacheOfAnUnknownLayoutIsIgnored();
    void cachedContentIsValidatedAgain();
    void unwritableCacheDoesNotBreakLoading();

    void autoRefreshSettings();

private:
    std::unique_ptr<RegistryManager> makeManager(const QUrl& url)
    {
        auto manager = std::make_unique<RegistryManager>(*m_client, m_cacheFile, url);
        FetchOptions options;
        options.maxAttempts = 2;
        options.retryDelayMs = 10;
        // Short, because Windows needs about two seconds to report a refused
        // connection to a closed local port. The local server answers at once.
        options.timeoutMs = 500;
        manager->setFetchOptions(options);
        return manager;
    }

    std::unique_ptr<RegistryManager> makeManager() { return makeManager(m_url); }

    /// Runs a refresh to its end: 1 on success, 0 on failure, -1 if it never finished.
    static int refreshAndWait(RegistryManager& manager)
    {
        QSignalSpy finished(&manager, &RegistryManager::refreshFinished);
        manager.refresh();
        if (!finished.wait(15000))
            return -1;
        return finished.at(0).at(0).toBool() ? 1 : 0;
    }

    /// Leaves a valid cache file behind, as a successful run would.
    void createCache()
    {
        auto manager = makeManager();
        m_next = body(validRegistry());
        QCOMPARE(refreshAndWait(*manager), 1);
        QVERIFY(QFileInfo::exists(m_cacheFile));
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_cacheFile;
    QUrl m_url; ///< Taken while the server listens: a closed server has no port any more.
    std::unique_ptr<GitHubClient> m_client;
    std::unique_ptr<TestHttpServer> m_server;
    Response m_next;
};

void RegistryManagerTests::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_cacheFile = m_dir->filePath(u"cache/registry.json"_s);
    m_client = std::make_unique<GitHubClient>(GitHubClient::Policy {{u"http"_s}, {u"127.0.0.1"_s}, true});
    m_next = body(validRegistry());
    m_server = std::make_unique<TestHttpServer>([this](const Request&) { return m_next; });
    QVERIFY(m_server->listen());
    m_url = m_server->url(u"/projects.json"_s);
}

void RegistryManagerTests::cleanup()
{
    m_server.reset();
    m_client.reset();
    m_dir.reset();
}

void RegistryManagerTests::refreshLoadsTheListAndWritesTheCache()
{
    auto manager = makeManager();
    QCOMPARE(manager->source(), Source::None);
    QVERIFY(manager->projects().isEmpty());
    QVERIFY(!manager->fetchedAt().isValid());

    QCOMPARE(refreshAndWait(*manager), 1);

    QCOMPARE(manager->source(), Source::Network);
    QCOMPARE(manager->problem(), Problem::None);
    QVERIFY(!manager->isLoading());
    QCOMPARE(manager->projects().size(), 2);
    QCOMPARE(manager->projects().at(0).id, u"alpha"_s);
    QCOMPARE(manager->projects().at(1).id, u"beta"_s);
    QCOMPARE(manager->registryName(), u"Test Registry"_s);
    QVERIFY(manager->fetchedAt().isValid());
    QVERIFY(manager->fetchedAt().secsTo(QDateTime::currentDateTimeUtc()) < 60);

    // What is on disk has the layout the manager reads back.
    const launcher::JsonFile::LoadResult cache = launcher::JsonFile::load(m_cacheFile);
    QCOMPARE(cache.status, launcher::JsonFile::Status::Ok);
    QCOMPARE(cache.object.value("cache_schema"_L1).toInt(), RegistryManager::kCacheSchemaVersion);
    QCOMPARE(cache.object.value("registry_url"_L1).toString(), m_server->url(u"/projects.json"_s).toString());
    QVERIFY(cache.object.value("fetched_at"_L1).toString().endsWith(u'Z'));
    QCOMPARE(cache.object.value("registry"_L1).toObject().value("projects"_L1).toArray().size(), 2);
}

void RegistryManagerTests::changedIsReportedForLoadingAndForTheResult()
{
    auto manager = makeManager();
    QSignalSpy changed(manager.get(), &RegistryManager::changed);

    manager->refresh();
    QCOMPARE(changed.count(), 1); // loading has started
    QVERIFY(manager->isLoading());

    QSignalSpy finished(manager.get(), &RegistryManager::refreshFinished);
    QVERIFY(finished.wait(10000));
    QCOMPARE(changed.count(), 2);
    QVERIFY(!manager->isLoading());
}

void RegistryManagerTests::cacheIsAvailableBeforeAnyNetworkAnswer()
{
    createCache();

    // A new start without a connection: the cache has to be enough.
    m_server->close();
    auto manager = makeManager();
    QSignalSpy changed(manager.get(), &RegistryManager::changed);
    manager->loadCache();

    QCOMPARE(manager->source(), Source::Cache);
    QCOMPARE(manager->problem(), Problem::None);
    QCOMPARE(manager->projects().size(), 2);
    QCOMPARE(manager->registryName(), u"Test Registry"_s);
    QVERIFY(manager->fetchedAt().isValid());
    QCOMPARE(changed.count(), 1);
}

void RegistryManagerTests::refreshWhileLoadingIsIgnored()
{
    auto manager = makeManager();
    manager->refresh();
    manager->refresh();
    manager->refresh();

    QSignalSpy finished(manager.get(), &RegistryManager::refreshFinished);
    QVERIFY(finished.wait(10000));
    QTest::qWait(100);
    QCOMPARE(finished.count(), 1);
    QCOMPARE(m_server->requestCount(), 1);
}

void RegistryManagerTests::noConnectionWithoutCache()
{
    m_server->close();
    auto manager = makeManager();
    QCOMPARE(refreshAndWait(*manager), 0);

    QCOMPARE(manager->source(), Source::None);
    QCOMPARE(manager->problem(), Problem::Unreachable);
    QVERIFY(manager->projects().isEmpty());
    QVERIFY(!manager->isLoading());
    QVERIFY(!QFileInfo::exists(m_cacheFile));
}

void RegistryManagerTests::noConnectionKeepsTheCachedList()
{
    createCache();
    m_server->close();

    auto manager = makeManager();
    manager->loadCache();
    const QDateTime cachedAt = manager->fetchedAt();
    QCOMPARE(refreshAndWait(*manager), 0);

    QCOMPARE(manager->source(), Source::Cache);
    QCOMPARE(manager->problem(), Problem::Unreachable);
    QCOMPARE(manager->projects().size(), 2);
    QCOMPARE(manager->fetchedAt(), cachedAt);
}

void RegistryManagerTests::httpErrorKeepsTheCachedList()
{
    createCache();
    auto manager = makeManager();
    manager->loadCache();

    m_next = body("Not Found", 404);
    QCOMPARE(refreshAndWait(*manager), 0);
    QCOMPARE(manager->problem(), Problem::Http);
    QCOMPARE(manager->httpStatus(), 404);
    QCOMPARE(manager->source(), Source::Cache);
    QCOMPARE(manager->projects().size(), 2);
}

void RegistryManagerTests::rateLimitIsReported()
{
    auto manager = makeManager();
    m_next = body("limit", 403);
    m_next.addHeader("X-RateLimit-Limit", "60");
    m_next.addHeader("X-RateLimit-Remaining", "0");

    QCOMPARE(refreshAndWait(*manager), 0);
    QCOMPARE(manager->problem(), Problem::RateLimited);
    QCOMPARE(manager->httpStatus(), 403);
    QCOMPARE(m_server->requestCount(), 1);
}

void RegistryManagerTests::brokenAnswerKeepsTheCachedList()
{
    createCache();
    const QByteArray cachedBefore = readFile(m_cacheFile);
    auto manager = makeManager();
    manager->loadCache();

    // A GitHub error page or a proxy login page answers with status 200.
    m_next = body("<html><body>Please sign in</body></html>");
    QCOMPARE(refreshAndWait(*manager), 0);
    QCOMPARE(manager->problem(), Problem::InvalidContent);
    QCOMPARE(manager->source(), Source::Cache);
    QCOMPARE(manager->projects().size(), 2);
    QCOMPARE(readFile(m_cacheFile), cachedBefore); // the good cache was not overwritten
}

void RegistryManagerTests::tooNewRegistryKeepsTheCachedList()
{
    createCache();
    const QByteArray cachedBefore = readFile(m_cacheFile);
    auto manager = makeManager();
    manager->loadCache();

    m_next = body(R"({"schema_version": 2, "projects": []})");
    QCOMPARE(refreshAndWait(*manager), 0);
    QCOMPARE(manager->problem(), Problem::UnsupportedVersion);
    QCOMPARE(manager->projects().size(), 2);
    QCOMPARE(readFile(m_cacheFile), cachedBefore);
}

void RegistryManagerTests::failedRefreshAfterSuccessKeepsTheListInMemory()
{
    auto manager = makeManager();
    QCOMPARE(refreshAndWait(*manager), 1);
    const QDateTime fetched = manager->fetchedAt();

    m_next = body("oops", 500);
    QCOMPARE(refreshAndWait(*manager), 0);
    QCOMPARE(manager->problem(), Problem::Http);
    QCOMPARE(manager->httpStatus(), 500);
    QCOMPARE(manager->source(), Source::Network); // still what the last good refresh brought
    QCOMPARE(manager->projects().size(), 2);
    QCOMPARE(manager->fetchedAt(), fetched);
}

void RegistryManagerTests::recoveryClearsTheProblem()
{
    auto manager = makeManager();
    m_next = body("oops", 503);
    QCOMPARE(refreshAndWait(*manager), 0);
    QCOMPARE(manager->problem(), Problem::Http);

    m_next = body(validRegistry());
    QCOMPARE(refreshAndWait(*manager), 1);
    QCOMPARE(manager->problem(), Problem::None);
    QCOMPARE(manager->httpStatus(), 0);
    QCOMPARE(manager->source(), Source::Network);
    QCOMPARE(manager->projects().size(), 2);
}

void RegistryManagerTests::invalidEntriesAreDroppedAndTheRestIsKept()
{
    m_next = body(R"({
        "schema_version": 1,
        "projects": [
            {"id": "good", "name": "Good", "description": "Fine", "owner": "KERSEX", "repository": "Good",
             "category": "Tools", "enabled": true, "asset_pattern": "Good.zip", "executable": "Good.exe",
             "install_directory": "Good"},
            {"id": "evil", "name": "Evil", "description": "Writes outside the library", "owner": "KERSEX",
             "repository": "Evil", "category": "Tools", "enabled": true, "asset_pattern": "Evil.zip",
             "executable": "Evil.exe", "install_directory": "../../Windows"}
        ]
    })");

    auto manager = makeManager();
    QCOMPARE(refreshAndWait(*manager), 1);
    QCOMPARE(manager->projects().size(), 1);
    QCOMPARE(manager->projects().first().id, u"good"_s);
    QCOMPARE(manager->issues().size(), 1);
    QCOMPARE(manager->issues().first().index, 1);

    // The next start sees the same: the rejected entry is not resurrected from the cache.
    auto restarted = makeManager();
    restarted->loadCache();
    QCOMPARE(restarted->projects().size(), 1);
    QCOMPARE(restarted->issues().size(), 1);
}

void RegistryManagerTests::answerWithoutAnyValidProjectReplacesTheList()
{
    // An empty registry is an answer like any other: the maintainers may have
    // withdrawn every project. Hiding that behind an old list would be wrong.
    auto manager = makeManager();
    QCOMPARE(refreshAndWait(*manager), 1);
    QCOMPARE(manager->projects().size(), 2);

    m_next = body(R"({"schema_version": 1, "projects": []})");
    QCOMPARE(refreshAndWait(*manager), 1);
    QVERIFY(manager->projects().isEmpty());
    QCOMPARE(manager->source(), Source::Network);
}

void RegistryManagerTests::corruptCacheFileIsMovedAside()
{
    const QByteArray garbage = "{\"cache_schema\": 1, \"registry\": {\"proj";
    QVERIFY(writeFile(m_cacheFile, garbage));

    auto manager = makeManager();
    manager->loadCache();
    QCOMPARE(manager->source(), Source::None);
    QVERIFY(manager->projects().isEmpty());

    // Kept for inspection under a new name, and no longer in the way.
    QVERIFY(!QFileInfo::exists(m_cacheFile));
    const QStringList aside =
        QDir(m_dir->filePath(u"cache"_s)).entryList({u"registry.corrupt-*.json"_s}, QDir::Files);
    QCOMPARE(aside.size(), 1);
    QCOMPARE(readFile(m_dir->filePath(u"cache/"_s + aside.first())), garbage);

    // The next successful refresh writes a fresh cache.
    QCOMPARE(refreshAndWait(*manager), 1);
    QVERIFY(QFileInfo::exists(m_cacheFile));
}

void RegistryManagerTests::cacheOfAnotherAddressIsIgnored()
{
    createCache();

    auto manager = makeManager(QUrl(u"http://127.0.0.1:1/other/projects.json"_s));
    manager->loadCache();
    QCOMPARE(manager->source(), Source::None);
    QVERIFY(manager->projects().isEmpty());
    QVERIFY(QFileInfo::exists(m_cacheFile)); // ignored, not deleted
}

void RegistryManagerTests::cacheOfAnUnknownLayoutIsIgnored()
{
    QJsonObject cache {
        {u"cache_schema"_s, 99},
        {u"fetched_at"_s, u"2026-11-10T12:00:00Z"_s},
        {u"registry_url"_s, m_server->url(u"/projects.json"_s).toString()},
        {u"registry"_s, QJsonDocument::fromJson(validRegistry()).object()},
    };
    QVERIFY(writeFile(m_cacheFile, QJsonDocument(cache).toJson()));

    auto manager = makeManager();
    manager->loadCache();
    QCOMPARE(manager->source(), Source::None);

    // Without a valid time stamp the cache is not trusted either.
    cache.insert(u"cache_schema"_s, 1);
    cache.insert(u"fetched_at"_s, u"yesterday"_s);
    QVERIFY(writeFile(m_cacheFile, QJsonDocument(cache).toJson()));
    manager->loadCache();
    QCOMPARE(manager->source(), Source::None);
}

void RegistryManagerTests::cachedContentIsValidatedAgain()
{
    // A cache file can be edited, damaged or left by another version. What it
    // contains must pass the same rules as a fresh answer.
    QJsonObject registry = QJsonDocument::fromJson(validRegistry()).object();
    QJsonArray projects = registry.value("projects"_L1).toArray();
    QJsonObject tampered = projects.at(0).toObject();
    tampered.insert(u"id"_s, u"tampered"_s);
    tampered.insert(u"install_directory"_s, u"..\\..\\Users"_s);
    projects.append(tampered);
    registry.insert(u"projects"_s, projects);

    const QJsonObject cache {
        {u"cache_schema"_s, 1},
        {u"fetched_at"_s, u"2026-11-10T12:00:00Z"_s},
        {u"registry_url"_s, m_server->url(u"/projects.json"_s).toString()},
        {u"registry"_s, registry},
    };
    QVERIFY(writeFile(m_cacheFile, QJsonDocument(cache).toJson()));

    auto manager = makeManager();
    manager->loadCache();
    QCOMPARE(manager->source(), Source::Cache);
    QCOMPARE(manager->projects().size(), 2);
    QCOMPARE(manager->issues().size(), 1);
}

void RegistryManagerTests::unwritableCacheDoesNotBreakLoading()
{
    // A file where the cache folder should be: saving fails, loading must not.
    QVERIFY(writeFile(m_dir->filePath(u"cache"_s), "not a folder"));

    auto manager = makeManager();
    QCOMPARE(refreshAndWait(*manager), 1);
    QCOMPARE(manager->source(), Source::Network);
    QCOMPARE(manager->projects().size(), 2);
}

void RegistryManagerTests::autoRefreshSettings()
{
    auto manager = makeManager();
    QVERIFY(!manager->autoRefreshActive());

    manager->setAutoRefresh(true, 30);
    QVERIFY(manager->autoRefreshActive());
    QCOMPARE(manager->autoRefreshIntervalMinutes(), 30);
    QCOMPARE(m_server->requestCount(), 0); // setting it up does not fetch

    manager->setAutoRefresh(true, 120);
    QVERIFY(manager->autoRefreshActive());
    QCOMPARE(manager->autoRefreshIntervalMinutes(), 120);

    manager->setAutoRefresh(false, 120);
    QVERIFY(!manager->autoRefreshActive());

    manager->setAutoRefresh(true, 0); // a nonsensical interval switches it off
    QVERIFY(!manager->autoRefreshActive());
}

QTEST_GUILESS_MAIN(RegistryManagerTests)
#include "RegistryManagerTests.moc"
