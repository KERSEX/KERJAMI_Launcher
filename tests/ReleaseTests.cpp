// Releases: reading GitHub's answer, choosing the newest stable release and
// the asset to install, and the ReleaseManager that fetches it. A server on
// the local machine plays GitHub.
//
// One test (liveGitHub) reads the release list of a public repository from the
// real GitHub. It only runs when LAUNCHER_LIVE_TESTS is set and downloads no files.

#include "TestHttpServer.h"
#include "github/GitHubClient.h"
#include "github/ReleaseManager.h"
#include "github/ReleaseParser.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTimer>

using namespace Qt::StringLiterals;
using launcher::FetchOptions;
using launcher::GitHubClient;
using launcher::Project;
using launcher::Release;
using launcher::ReleaseAsset;
using launcher::ReleaseEndpoints;
using launcher::ReleaseListResult;
using launcher::ReleaseManager;
using launcher::ReleaseSelection;
using Problem = launcher::ReleaseManager::Problem;
namespace RP = launcher::ReleaseParser;
using Response = TestHttpServer::Response;
using Request = TestHttpServer::Request;

namespace {

const QString kDigest = u"3a7bd3e2360a3d29eea436fcfb7e44c735d117c42d1c1835420b6b9942dd4f1b"_s;

QJsonObject asset(const QString& name, qint64 size = 1000, const QString& digest = {})
{
    QJsonObject object {
        {u"name"_s, name},
        {u"size"_s, size},
        {u"state"_s, u"uploaded"_s},
        {u"content_type"_s, u"application/zip"_s},
        // The address GitHub reports. The launcher must never use it.
        {u"browser_download_url"_s, u"https://evil.example/"_s + name},
    };
    // GitHub reports the digest as "sha256:<hex>".
    object.insert(
        u"digest"_s, digest.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(u"sha256:"_s + digest));
    return object;
}

QJsonObject release(const QString& tag, const QJsonArray& assets, bool prerelease = false, bool draft = false)
{
    return {
        {u"tag_name"_s, tag},
        {u"name"_s, u"Release "_s + tag},
        {u"body"_s, u"Notes for "_s + tag},
        {u"draft"_s, draft},
        {u"prerelease"_s, prerelease},
        {u"published_at"_s, u"2026-11-01T12:00:00Z"_s},
        {u"assets"_s, assets},
    };
}

QByteArray list(const QJsonArray& releases)
{
    return QJsonDocument(releases).toJson();
}

Project demoProject(const QString& pattern = u"Demo-*.zip"_s)
{
    Project project;
    project.id = u"demo"_s;
    project.name = u"Demo"_s;
    project.owner = u"KERSEX"_s;
    project.repository = u"Demo"_s;
    project.assetPattern = pattern;
    project.executable = u"Demo.exe"_s;
    project.installDirectory = u"Demo"_s;
    return project;
}

Response body(const QByteArray& content, int status = 200)
{
    Response response;
    response.status = status;
    response.body = content;
    return response;
}

struct Outcome {
    bool ready = false;
    bool failed = false;
    ReleaseSelection selection;
    Problem problem = Problem::None;
    int httpStatus = 0;
};

} // namespace

class ReleaseTests : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Parser
    void parsesAStableRelease();
    void digestIsTakenOverOnlyInTheExpectedShape_data();
    void digestIsTakenOverOnlyInTheExpectedShape();
    void unusableAssetsAreLeftOut_data();
    void unusableAssetsAreLeftOut();
    void draftsPreReleasesAndOddTagsAreSkipped();
    void answersThatAreNoReleaseList_data();
    void answersThatAreNoReleaseList();
    void entriesThatAreNotObjectsAreSkipped();
    void limits();
    void newestIsChosenByVersionNotByPosition();
    void newestOfEqualVersionsIsTheFirst();
    void assetPatterns_data();
    void assetPatterns();
    void assetFileNames_data();
    void assetFileNames();

    // Manager
    void managerSelectsTheNewestRelease();
    void downloadAddressIsBuiltNotTaken();
    void tagWithBuildMetadataGivesAValidAddress();
    void managerReportsNoStableRelease();
    void newestReleaseWithoutTheFileIsNotReplacedByAnOlderOne();
    void ambiguousAssetsAreNotChosen();
    void invalidProjectsAreRejectedWithoutARequest_data();
    void invalidProjectsAreRejectedWithoutARequest();
    void httpErrorsAreReported();
    void rateLimitIsReported();
    void noConnectionIsReported();
    void unusableAnswerIsReported();
    void failedFetchKeepsTheEarlierResult();
    void secondFetchForTheSameProjectIsIgnored();
    void differentProjectsRunSideBySide();

    void liveGitHub();

private:
    Outcome fetch(ReleaseManager& manager, const Project& project)
    {
        Outcome outcome;
        QEventLoop loop;
        connect(&manager, &ReleaseManager::releaseReady, &loop,
            [&](const QString& id, const ReleaseSelection& selection) {
                if (id != project.id)
                    return;
                outcome.ready = true;
                outcome.selection = selection;
                loop.quit();
            });
        connect(&manager, &ReleaseManager::fetchFailed, &loop,
            [&](const QString& id, Problem problem, int status) {
                if (id != project.id)
                    return;
                outcome.failed = true;
                outcome.problem = problem;
                outcome.httpStatus = status;
                loop.quit();
            });
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        manager.fetchLatest(project);
        loop.exec();
        return outcome;
    }

    std::unique_ptr<ReleaseManager> makeManager()
    {
        const ReleaseEndpoints endpoints {m_server->url(), m_server->url()};
        auto manager = std::make_unique<ReleaseManager>(*m_client, endpoints);
        FetchOptions options;
        options.accept = "application/vnd.github+json";
        options.maxAttempts = 2;
        options.retryDelayMs = 10;
        options.timeoutMs = 500; // a closed local port takes about two seconds to refuse on Windows
        manager->setFetchOptions(options);
        return manager;
    }

    std::unique_ptr<GitHubClient> m_client;
    std::unique_ptr<TestHttpServer> m_server;
    Response m_next;
};

void ReleaseTests::init()
{
    m_client = std::make_unique<GitHubClient>(GitHubClient::Policy {{u"http"_s}, {u"127.0.0.1"_s}, true});
    m_next = body(list({release(u"v1.0.0"_s, {asset(u"Demo-1.0.0.zip"_s)})}));
    m_server = std::make_unique<TestHttpServer>([this](const Request&) { return m_next; });
    QVERIFY(m_server->listen());
}

void ReleaseTests::cleanup()
{
    m_server.reset();
    m_client.reset();
}

// ---- Parser ----------------------------------------------------------------

void ReleaseTests::parsesAStableRelease()
{
    QJsonObject object = release(
        u"v1.2.3"_s, {asset(u"Demo-1.2.3.zip"_s, 56'200'000, kDigest), asset(u"Demo-1.2.3.zip.asc"_s, 833)});
    object.insert(u"name"_s, u"  Demo 1.2.3  "_s);
    object.insert(u"body"_s, u"## Changes\n- faster\n- fixes"_s);

    const ReleaseListResult result = RP::parseList(list({object}));
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.releases.size(), 1);
    QCOMPARE(result.skipped, 0);

    const Release& parsed = result.releases.first();
    QCOMPARE(parsed.tag, u"v1.2.3"_s);
    QCOMPARE(parsed.version.toString(), u"1.2.3"_s);
    QCOMPARE(parsed.title, u"Demo 1.2.3"_s);
    QCOMPARE(parsed.notes, u"## Changes\n- faster\n- fixes"_s);
    QCOMPARE(parsed.publishedAt, QDateTime::fromString(u"2026-11-01T12:00:00Z"_s, Qt::ISODate));
    QVERIFY(parsed.publishedAt.isValid());

    QCOMPARE(parsed.assets.size(), 2);
    QCOMPARE(parsed.assets.at(0).name, u"Demo-1.2.3.zip"_s);
    QCOMPARE(parsed.assets.at(0).size, 56'200'000LL);
    QCOMPARE(parsed.assets.at(0).sha256, kDigest);
    QCOMPARE(parsed.assets.at(1).name, u"Demo-1.2.3.zip.asc"_s);
    QVERIFY(parsed.assets.at(1).sha256.isEmpty());
}

void ReleaseTests::digestIsTakenOverOnlyInTheExpectedShape_data()
{
    QTest::addColumn<QJsonValue>("digest");
    QTest::addColumn<QString>("expected");

    QTest::newRow("sha256") << QJsonValue(u"sha256:"_s + kDigest) << kDigest;
    QTest::newRow("uppercase hex is lowered") << QJsonValue(u"sha256:"_s + kDigest.toUpper()) << kDigest;
    QTest::newRow("null") << QJsonValue(QJsonValue::Null) << QString();
    QTest::newRow("missing") << QJsonValue(QJsonValue::Undefined) << QString();
    QTest::newRow("other algorithm") << QJsonValue(u"sha512:"_s + QString(128, u'a')) << QString();
    QTest::newRow("md5") << QJsonValue(u"md5:"_s + QString(32, u'a')) << QString();
    QTest::newRow("no prefix") << QJsonValue(kDigest) << QString();
    QTest::newRow("too short") << QJsonValue(u"sha256:"_s + kDigest.left(63)) << QString();
    QTest::newRow("too long") << QJsonValue(u"sha256:"_s + kDigest + u"a"_s) << QString();
    QTest::newRow("not hex") << QJsonValue(u"sha256:"_s + QString(64, u'g')) << QString();
    QTest::newRow("trailing newline") << QJsonValue(u"sha256:"_s + kDigest + u"\n"_s) << QString();
    QTest::newRow("a number") << QJsonValue(5) << QString();
}

void ReleaseTests::digestIsTakenOverOnlyInTheExpectedShape()
{
    QFETCH(QJsonValue, digest);
    QFETCH(QString, expected);

    QJsonObject object = asset(u"Demo.zip"_s);
    if (digest.isUndefined())
        object.remove(u"digest"_s);
    else
        object.insert(u"digest"_s, digest);

    const ReleaseListResult result = RP::parseList(list({release(u"v1.0.0"_s, {object})}));
    QVERIFY(result.ok);
    QCOMPARE(result.releases.size(), 1);
    QCOMPARE(result.releases.first().assets.size(), 1);
    QCOMPARE(result.releases.first().assets.first().sha256, expected);
}

void ReleaseTests::unusableAssetsAreLeftOut_data()
{
    QTest::addColumn<QString>("key");
    QTest::addColumn<QJsonValue>("value");

    QTest::newRow("still uploading") << u"state"_s << QJsonValue(u"starter"_s);
    QTest::newRow("state missing") << u"state"_s << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("size zero") << u"size"_s << QJsonValue(0);
    QTest::newRow("size negative") << u"size"_s << QJsonValue(-5);
    QTest::newRow("size as text") << u"size"_s << QJsonValue(u"1000"_s);
    QTest::newRow("size missing") << u"size"_s << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("size above the limit")
        << u"size"_s << QJsonValue(static_cast<double>(RP::kMaxAssetBytes + 1));
    QTest::newRow("name missing") << u"name"_s << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("name with slash") << u"name"_s << QJsonValue(u"sub/Demo.zip"_s);
    QTest::newRow("name with backslash") << u"name"_s << QJsonValue(u"sub\\Demo.zip"_s);
    QTest::newRow("name with parent folder") << u"name"_s << QJsonValue(u"..\\Demo.zip"_s);
    QTest::newRow("name is a device") << u"name"_s << QJsonValue(u"con.zip"_s);
    QTest::newRow("name starts with a dot") << u"name"_s << QJsonValue(u".hidden.zip"_s);
    QTest::newRow("name with space") << u"name"_s << QJsonValue(u"My Demo.zip"_s);
    QTest::newRow("name with colon") << u"name"_s << QJsonValue(u"Demo.zip:stream"_s);
    QTest::newRow("name too long") << u"name"_s << QJsonValue(QString(125, u'a') + u".zip"_s);
    QTest::newRow("name empty") << u"name"_s << QJsonValue(QString());
}

void ReleaseTests::unusableAssetsAreLeftOut()
{
    QFETCH(QString, key);
    QFETCH(QJsonValue, value);

    QJsonObject broken = asset(u"Broken.zip"_s);
    if (value.isUndefined())
        broken.remove(key);
    else
        broken.insert(key, value);

    const ReleaseListResult result =
        RP::parseList(list({release(u"v1.0.0"_s, {asset(u"Good.zip"_s), broken})}));
    QVERIFY(result.ok);
    QCOMPARE(result.releases.size(), 1);
    // The release stays; only the unusable file is dropped.
    QCOMPARE(result.releases.first().assets.size(), 1);
    QCOMPARE(result.releases.first().assets.first().name, u"Good.zip"_s);
}

void ReleaseTests::draftsPreReleasesAndOddTagsAreSkipped()
{
    const QJsonArray assets {asset(u"Demo.zip"_s)};
    const QByteArray json = list({
        release(u"v2.0.0"_s, assets, /*prerelease*/ true),
        release(u"v1.9.0"_s, assets, false, /*draft*/ true),
        release(u"v1.8.0-beta.1"_s, assets),
        release(u"v1.8.0-rc.2"_s, assets, false, false),
        release(u"latest"_s, assets),
        release(u"KERS_SubsystemsV0.3.1"_s, assets),
        release(u"1.0"_s, assets),
        release(u"v1.0.0"_s, assets),
    });

    const ReleaseListResult result = RP::parseList(json);
    QVERIFY(result.ok);
    QCOMPARE(result.releases.size(), 1);
    QCOMPARE(result.releases.first().tag, u"v1.0.0"_s);
    QCOMPARE(result.skipped, 7);
}

void ReleaseTests::answersThatAreNoReleaseList_data()
{
    QTest::addColumn<QByteArray>("json");

    QTest::newRow("empty") << QByteArray();
    QTest::newRow("html") << QByteArray("<html>rate limit</html>");
    QTest::newRow("truncated") << QByteArray(R"([{"tag_name": "v1.0.0", "assets": [)");
    QTest::newRow("an object") << QByteArray(R"({"message": "Not Found"})");
    QTest::newRow("text") << QByteArray(R"("releases")");
}

void ReleaseTests::answersThatAreNoReleaseList()
{
    QFETCH(QByteArray, json);
    const ReleaseListResult result = RP::parseList(json);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.releases.isEmpty());
}

void ReleaseTests::entriesThatAreNotObjectsAreSkipped()
{
    const ReleaseListResult result =
        RP::parseList(list({5, u"text"_s, QJsonValue::Null, release(u"v1.0.0"_s, {asset(u"Demo.zip"_s)})}));
    QVERIFY(result.ok);
    QCOMPARE(result.releases.size(), 1);
    QCOMPARE(result.skipped, 3);

    // An empty list is a valid answer: the repository has no releases.
    const ReleaseListResult none = RP::parseList(list({}));
    QVERIFY(none.ok);
    QVERIFY(none.releases.isEmpty());
}

void ReleaseTests::limits()
{
    QJsonArray manyReleases;
    for (int i = 0; i < RP::kMaxReleases + 5; ++i)
        manyReleases.append(release(u"v1.0.%1"_s.arg(i), {asset(u"Demo.zip"_s)}));
    QCOMPARE(RP::parseList(list(manyReleases)).releases.size(), RP::kMaxReleases);

    QJsonArray manyAssets;
    for (int i = 0; i < RP::kMaxAssetsPerRelease + 5; ++i)
        manyAssets.append(asset(u"Demo-%1.zip"_s.arg(i)));
    QCOMPARE(RP::parseList(list({release(u"v1.0.0"_s, manyAssets)})).releases.first().assets.size(),
        RP::kMaxAssetsPerRelease);

    QJsonObject longNotes = release(u"v1.0.0"_s, {});
    longNotes.insert(u"body"_s, QString(RP::kMaxNotesLength + 1000, u'x'));
    longNotes.insert(u"name"_s, QString(500, u'n'));
    const Release parsed = RP::parseList(list({longNotes})).releases.first();
    QCOMPARE(parsed.notes.size(), RP::kMaxNotesLength);
    QVERIFY(parsed.title.size() <= 200);
}

void ReleaseTests::newestIsChosenByVersionNotByPosition()
{
    // GitHub lists by date. A hotfix of an old line published last must not count as the newest.
    const QJsonArray assets {asset(u"Demo.zip"_s)};
    const ReleaseListResult result = RP::parseList(list({
        release(u"v1.4.1"_s, assets),
        release(u"v2.0.0"_s, assets),
        release(u"v1.10.0"_s, assets),
        release(u"v1.9.9"_s, assets),
    }));
    const Release* newest = RP::latest(result.releases);
    QVERIFY(newest);
    QCOMPARE(newest->tag, u"v2.0.0"_s);

    QVERIFY(RP::latest({}) == nullptr);
}

void ReleaseTests::newestOfEqualVersionsIsTheFirst()
{
    const QJsonArray assets {asset(u"Demo.zip"_s)};
    const ReleaseListResult result = RP::parseList(list({
        release(u"v1.0.0"_s, assets),
        release(u"1.0.0"_s, assets),
        release(u"v1.0.0+rebuild"_s, assets),
    }));
    QCOMPARE(RP::latest(result.releases)->tag, u"v1.0.0"_s);
}

void ReleaseTests::assetPatterns_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QStringList>("matches");

    const QStringList names = {u"Demo-1.0.0-Windows-x64.zip"_s, u"Demo-1.0.0-Linux-x64.zip"_s,
        u"Demo-1.0.0-Windows-x64.zip.asc"_s, u"Demo.zip"_s, u"DemoXzip"_s, u"demo.ZIP"_s};
    QTest::addColumn<QStringList>("names");

    QTest::newRow("exact name") << u"Demo.zip"_s << QStringList {u"Demo.zip"_s, u"demo.ZIP"_s} << names;
    QTest::newRow("a dot is a dot") << u"Demo.zip"_s << QStringList {u"Demo.zip"_s, u"demo.ZIP"_s} << names;
    QTest::newRow("wildcard in the middle")
        << u"Demo-*-Windows-x64.zip"_s << QStringList {u"Demo-1.0.0-Windows-x64.zip"_s} << names;
    QTest::newRow("wildcard at the end is anchored")
        << u"Demo-1.0.0-*.zip"_s
        << QStringList {u"Demo-1.0.0-Windows-x64.zip"_s, u"Demo-1.0.0-Linux-x64.zip"_s} << names;
    QTest::newRow("two wildcards") << u"*Windows*zip"_s << QStringList {u"Demo-1.0.0-Windows-x64.zip"_s}
                                   << names;
    QTest::newRow("only a wildcard") << u"*"_s << names << names;
    QTest::newRow("nothing fits") << u"Other-*.zip"_s << QStringList() << names;
    QTest::newRow("question mark is literal") << u"Demo?zip"_s << QStringList() << names;
    QTest::newRow("brackets are literal") << u"[D]emo.zip"_s << QStringList() << names;
    QTest::newRow("must fit completely") << u"Demo"_s << QStringList() << names;
}

void ReleaseTests::assetPatterns()
{
    QFETCH(QString, pattern);
    QFETCH(QStringList, matches);
    QFETCH(QStringList, names);

    Release release;
    for (const QString& name : names) {
        ReleaseAsset entry;
        entry.name = name;
        entry.size = 1;
        release.assets.append(entry);
    }

    QStringList found;
    for (const ReleaseAsset& entry : RP::matchingAssets(release, pattern))
        found.append(entry.name);
    QCOMPARE(found, matches);
}

void ReleaseTests::assetFileNames_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("valid");

    QTest::newRow("typical") << u"KERS_Overlay-1.2.0-Windows-x64.zip"_s << true;
    QTest::newRow("brackets and plus") << u"app+(1).zip"_s << true;
    QTest::newRow("128 characters") << QString(124, u'a') + u".zip"_s << true;
    QTest::newRow("129 characters") << QString(125, u'a') + u".zip"_s << false;
    QTest::newRow("device name") << u"CON.zip"_s << false;
    QTest::newRow("numbered device") << u"lpt1.zip"_s << false;
    QTest::newRow("looks like a device") << u"console.zip"_s << true;
    QTest::newRow("leading dot") << u".zip"_s << false;
    QTest::newRow("hyphen before the extension") << u"a-.zip"_s << true;
    QTest::newRow("trailing dot") << u"app."_s << false;
    QTest::newRow("slash") << u"a/b.zip"_s << false;
    QTest::newRow("backslash") << u"a\\b.zip"_s << false;
    QTest::newRow("colon") << u"a:b.zip"_s << false;
    QTest::newRow("space") << u"a b.zip"_s << false;
    QTest::newRow("umlaut") << u"Übung.zip"_s << false;
    QTest::newRow("wildcard") << u"a*.zip"_s << false;
    QTest::newRow("trailing newline") << u"a.zip\n"_s << false;
    QTest::newRow("empty") << QString() << false;
}

void ReleaseTests::assetFileNames()
{
    QFETCH(QString, name);
    QFETCH(bool, valid);
    QCOMPARE(RP::isValidAssetFileName(name), valid);
}

// ---- Manager -----------------------------------------------------------------

void ReleaseTests::managerSelectsTheNewestRelease()
{
    m_next = body(list({
        release(u"v1.1.0"_s, {asset(u"Demo-1.1.0.zip"_s, 2000, kDigest)}),
        release(u"v1.2.0-rc.1"_s, {asset(u"Demo-1.2.0-rc.1.zip"_s)}, /*prerelease*/ true),
        release(u"v1.2.0"_s,
            {asset(u"Demo-1.2.0.zip"_s, 3000, kDigest), asset(u"Demo-1.2.0.zip.asc"_s, 833),
                asset(u"Notes.txt"_s, 10)}),
        release(u"v1.0.5"_s, {asset(u"Demo-1.0.5.zip"_s)}),
    }));

    auto manager = makeManager();
    const Project project = demoProject();
    const Outcome outcome = fetch(*manager, project);

    QVERIFY(outcome.ready);
    QCOMPARE(outcome.selection.release.tag, u"v1.2.0"_s);
    QCOMPARE(outcome.selection.release.version.toString(), u"1.2.0"_s);
    QCOMPARE(outcome.selection.release.notes, u"Notes for v1.2.0"_s);
    QCOMPARE(outcome.selection.asset.name, u"Demo-1.2.0.zip"_s);
    QCOMPARE(outcome.selection.asset.size, 3000LL);
    QCOMPARE(outcome.selection.asset.sha256, kDigest);

    QCOMPARE(m_server->requestCount(), 1);
    const Request& request = m_server->requests().first();
    QCOMPARE(request.path, u"/repos/KERSEX/Demo/releases?per_page=30"_s);
    QCOMPARE(request.headers.value("accept"), QByteArray("application/vnd.github+json"));

    QVERIFY(!manager->isFetching(project.id));
    const std::optional<ReleaseSelection> kept = manager->latest(project.id);
    QVERIFY(kept.has_value());
    QCOMPARE(kept->release.tag, u"v1.2.0"_s);
    QVERIFY(!manager->latest(u"other"_s).has_value());
}

void ReleaseTests::downloadAddressIsBuiltNotTaken()
{
    m_next = body(list({release(u"v1.2.0"_s, {asset(u"Demo-1.2.0.zip"_s)})}));
    auto manager = makeManager();
    const Outcome outcome = fetch(*manager, demoProject());
    QVERIFY(outcome.ready);

    // The answer names https://evil.example/ as the download address.
    const QUrl url = outcome.selection.downloadUrl;
    QCOMPARE(url.host(), u"127.0.0.1"_s);
    QCOMPARE(url.port(), static_cast<int>(m_server->port()));
    QCOMPARE(url.path(), u"/KERSEX/Demo/releases/download/v1.2.0/Demo-1.2.0.zip"_s);
    QVERIFY(!url.toString().contains(u"evil"_s));
}

void ReleaseTests::tagWithBuildMetadataGivesAValidAddress()
{
    m_next = body(list({release(u"v1.0.0+build.7"_s, {asset(u"Demo-1.0.0.zip"_s)})}));
    auto manager = makeManager();
    const Outcome outcome = fetch(*manager, demoProject());
    QVERIFY(outcome.ready);
    QCOMPARE(outcome.selection.downloadUrl.path(),
        u"/KERSEX/Demo/releases/download/v1.0.0+build.7/Demo-1.0.0.zip"_s);
    QVERIFY(outcome.selection.downloadUrl.isValid());
}

void ReleaseTests::managerReportsNoStableRelease()
{
    const QJsonArray assets {asset(u"Demo-2.0.0.zip"_s)};
    m_next =
        body(list({release(u"v2.0.0-beta.1"_s, assets, true), release(u"v1.0.0"_s, assets, false, true)}));
    auto manager = makeManager();
    Outcome outcome = fetch(*manager, demoProject());
    QVERIFY(outcome.failed);
    QCOMPARE(outcome.problem, Problem::NoRelease);

    // A repository without any release says the same.
    m_next = body(list({}));
    outcome = fetch(*manager, demoProject());
    QCOMPARE(outcome.problem, Problem::NoRelease);
}

void ReleaseTests::newestReleaseWithoutTheFileIsNotReplacedByAnOlderOne()
{
    // The newest release may still be uploading. Offering 1.0.0 then would look
    // like an update and install an old version.
    m_next = body(list({
        release(u"v2.0.0"_s, {asset(u"Readme.txt"_s)}),
        release(u"v1.0.0"_s, {asset(u"Demo-1.0.0.zip"_s)}),
    }));
    auto manager = makeManager();
    const Outcome outcome = fetch(*manager, demoProject());
    QVERIFY(outcome.failed);
    QCOMPARE(outcome.problem, Problem::NoMatchingAsset);
}

void ReleaseTests::ambiguousAssetsAreNotChosen()
{
    m_next = body(list({release(u"v1.0.0"_s, {asset(u"Demo-Windows.zip"_s), asset(u"Demo-Linux.zip"_s)})}));
    auto manager = makeManager();

    Outcome outcome = fetch(*manager, demoProject(u"*.zip"_s));
    QVERIFY(outcome.failed);
    QCOMPARE(outcome.problem, Problem::AmbiguousAsset);

    // A pattern that names one file is fine.
    outcome = fetch(*manager, demoProject(u"Demo-Windows.zip"_s));
    QVERIFY(outcome.ready);
    QCOMPARE(outcome.selection.asset.name, u"Demo-Windows.zip"_s);
}

void ReleaseTests::invalidProjectsAreRejectedWithoutARequest_data()
{
    QTest::addColumn<QString>("field");
    QTest::addColumn<QString>("value");

    QTest::newRow("owner with space") << u"owner"_s << u"bad owner"_s;
    QTest::newRow("owner with slash") << u"owner"_s << u"a/b"_s;
    QTest::newRow("owner with query") << u"owner"_s << u"x?y=1"_s;
    QTest::newRow("repository parent") << u"repository"_s << u".."_s;
    QTest::newRow("repository with slash") << u"repository"_s << u"a/../b"_s;
    QTest::newRow("repository with git suffix") << u"repository"_s << u"Demo.git"_s;
    QTest::newRow("id with capital") << u"id"_s << u"Demo"_s;
    QTest::newRow("pattern is not a zip") << u"assetPattern"_s << u"Demo.exe"_s;
    QTest::newRow("pattern with path") << u"assetPattern"_s << u"../Demo.zip"_s;
}

void ReleaseTests::invalidProjectsAreRejectedWithoutARequest()
{
    QFETCH(QString, field);
    QFETCH(QString, value);

    Project project = demoProject();
    if (field == u"owner"_s)
        project.owner = value;
    else if (field == u"repository"_s)
        project.repository = value;
    else if (field == u"id"_s)
        project.id = value;
    else
        project.assetPattern = value;

    auto manager = makeManager();
    bool answeredInsideTheCall = false;
    connect(manager.get(), &ReleaseManager::fetchFailed, this, [&] { answeredInsideTheCall = true; });
    QEventLoop loop;
    Outcome outcome;
    connect(manager.get(), &ReleaseManager::fetchFailed, &loop, [&](const QString&, Problem problem, int) {
        outcome.failed = true;
        outcome.problem = problem;
        loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    manager->fetchLatest(project);
    QVERIFY(!answeredInsideTheCall); // like every answer: not before the call has returned
    loop.exec();

    QVERIFY(outcome.failed);
    QCOMPARE(outcome.problem, Problem::InvalidProject);
    QCOMPARE(m_server->requestCount(), 0);
}

void ReleaseTests::httpErrorsAreReported()
{
    m_next = body("Not Found", 404);
    auto manager = makeManager();
    const Outcome outcome = fetch(*manager, demoProject());
    QVERIFY(outcome.failed);
    QCOMPARE(outcome.problem, Problem::Http);
    QCOMPARE(outcome.httpStatus, 404);
}

void ReleaseTests::rateLimitIsReported()
{
    m_next = body("API rate limit exceeded", 403);
    m_next.addHeader("X-RateLimit-Limit", "60");
    m_next.addHeader("X-RateLimit-Remaining", "0");
    m_next.addHeader("X-RateLimit-Reset", "1893456000");

    auto manager = makeManager();
    const Outcome outcome = fetch(*manager, demoProject());
    QVERIFY(outcome.failed);
    QCOMPARE(outcome.problem, Problem::RateLimited);
    QCOMPARE(m_server->requestCount(), 1); // not repeated: that would only use up more of the budget
    QVERIFY(m_client->rateLimit().isKnown());
    QCOMPARE(m_client->rateLimit().remaining, 0);
}

void ReleaseTests::noConnectionIsReported()
{
    const ReleaseEndpoints endpoints {m_server->url(), m_server->url()};
    m_server->close();
    ReleaseManager manager(*m_client, endpoints);
    FetchOptions options;
    options.maxAttempts = 1;
    options.timeoutMs = 500;
    manager.setFetchOptions(options);

    const Outcome outcome = fetch(manager, demoProject());
    QVERIFY(outcome.failed);
    QCOMPARE(outcome.problem, Problem::Unreachable);
}

void ReleaseTests::unusableAnswerIsReported()
{
    m_next = body("<html>Service unavailable</html>");
    auto manager = makeManager();
    const Outcome outcome = fetch(*manager, demoProject());
    QVERIFY(outcome.failed);
    QCOMPARE(outcome.problem, Problem::InvalidContent);
}

void ReleaseTests::failedFetchKeepsTheEarlierResult()
{
    auto manager = makeManager();
    QVERIFY(fetch(*manager, demoProject()).ready);

    m_next = body("oops", 500);
    const Outcome outcome = fetch(*manager, demoProject());
    QVERIFY(outcome.failed);
    QCOMPARE(outcome.problem, Problem::Http);

    // A failed check must not make an installed project look as if it had no release.
    const std::optional<ReleaseSelection> kept = manager->latest(u"demo"_s);
    QVERIFY(kept.has_value());
    QCOMPARE(kept->release.tag, u"v1.0.0"_s);
}

void ReleaseTests::secondFetchForTheSameProjectIsIgnored()
{
    auto manager = makeManager();
    int answers = 0;
    connect(manager.get(), &ReleaseManager::releaseReady, this, [&answers] { ++answers; });

    manager->fetchLatest(demoProject());
    QVERIFY(manager->isFetching(u"demo"_s));
    manager->fetchLatest(demoProject());
    manager->fetchLatest(demoProject());

    QTRY_COMPARE_WITH_TIMEOUT(answers, 1, 5000);
    QTest::qWait(100);
    QCOMPARE(answers, 1);
    QCOMPARE(m_server->requestCount(), 1);
}

void ReleaseTests::differentProjectsRunSideBySide()
{
    auto manager = makeManager();
    QStringList answered;
    connect(manager.get(), &ReleaseManager::releaseReady, this,
        [&answered](const QString& id) { answered.append(id); });

    Project other = demoProject();
    other.id = u"other"_s;
    other.repository = u"Other"_s;
    manager->fetchLatest(demoProject());
    manager->fetchLatest(other);

    QTRY_COMPARE_WITH_TIMEOUT(answered.size(), 2, 5000);
    QVERIFY(answered.contains(u"demo"_s));
    QVERIFY(answered.contains(u"other"_s));
    QCOMPARE(m_server->requestCount(), 2);
}

void ReleaseTests::liveGitHub()
{
    if (!qEnvironmentVariableIsSet("LAUNCHER_LIVE_TESTS"))
        QSKIP("Set LAUNCHER_LIVE_TESTS=1 to read the release list of a public repository from GitHub.");

    // Metadata only: one request to the API, no file is downloaded.
    GitHubClient client(GitHubClient::Policy::production());
    ReleaseManager manager(client);
    Project project = demoProject(u"include.zip"_s);
    project.owner = u"nlohmann"_s;
    project.repository = u"json"_s;

    const Outcome outcome = fetch(manager, project);
    QVERIFY2(outcome.ready,
        qPrintable(u"problem %1, HTTP %2"_s.arg(static_cast<int>(outcome.problem)).arg(outcome.httpStatus)));
    QVERIFY(outcome.selection.release.version > launcher::SemVer::parse(u"3.0.0"_s).value());
    QCOMPARE(outcome.selection.asset.name, u"include.zip"_s);
    QVERIFY(outcome.selection.asset.size > 1000);
    QCOMPARE(outcome.selection.downloadUrl.host(), u"github.com"_s);
    QVERIFY(outcome.selection.downloadUrl.path().startsWith(u"/nlohmann/json/releases/download/v"_s));
    QVERIFY(client.rateLimit().isKnown());
    qInfo() << "Newest release:" << outcome.selection.release.tag << outcome.selection.asset.name
            << outcome.selection.asset.size << "bytes, digest"
            << (outcome.selection.asset.sha256.isEmpty() ? "none" : "present") << "- request budget"
            << client.rateLimit().remaining << "of" << client.rateLimit().limit;
}

QTEST_GUILESS_MAIN(ReleaseTests)
#include "ReleaseTests.moc"
