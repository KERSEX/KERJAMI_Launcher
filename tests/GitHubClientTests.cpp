// GitHub client: the URL policy, retries, rate limits, size limits and
// redirects. A server on the local machine plays GitHub; the policy of the
// clients below allows exactly that machine, nothing else.
//
// One test (liveGitHub) talks to the real GitHub. It only runs when the
// environment variable LAUNCHER_LIVE_TESTS is set.

#include "TestHttpServer.h"
#include "app/Branding.h"
#include "github/GitHubClient.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTest>
#include <QTimer>

using namespace Qt::StringLiterals;
using launcher::FetchOptions;
using launcher::GitHubClient;
using launcher::GitHubRequest;
using launcher::NetworkError;
using launcher::NetworkResult;
using launcher::RateLimit;
using Response = TestHttpServer::Response;
using Request = TestHttpServer::Request;

namespace {

/// May talk to this machine over plain HTTP, on any port.
GitHubClient::Policy localPolicy()
{
    return GitHubClient::Policy {{u"http"_s}, {u"127.0.0.1"_s, u"localhost"_s}, true};
}

FetchOptions quickOptions()
{
    FetchOptions options;
    options.retryDelayMs = 10;
    options.timeoutMs = 2000;
    options.maxAttempts = 3;
    return options;
}

/// Runs a request to the end. A request that does not finish within ten
/// seconds comes back as an aborted result with the detail "TEST TIMEOUT".
NetworkResult fetch(GitHubClient& client, const QUrl& url, const FetchOptions& options = quickOptions())
{
    NetworkResult result;
    result.error = NetworkError::Aborted;
    result.detail = u"TEST TIMEOUT"_s;

    QEventLoop loop;
    QPointer<GitHubRequest> request = client.get(url, options);
    QObject::connect(request, &GitHubRequest::finished, &loop, [&](const NetworkResult& finished) {
        result = finished;
        loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    return result;
}

Response answer(int status, const QByteArray& body = {})
{
    Response response;
    response.status = status;
    response.body = body;
    return response;
}

Response redirect(const QString& location)
{
    Response response = answer(302);
    response.addHeader("Location", location.toUtf8());
    return response;
}

} // namespace

class GitHubClientTests : public QObject {
    Q_OBJECT

private slots:
    void productionPolicy_data();
    void productionPolicy();
    void blockedAddressFailsWithoutNetwork();
    void finishedIsNeverEmittedBeforeGetReturns();

    void successfulGet();
    void requestIdentifiesTheLauncher();
    void rateLimitHeadersAreTracked();

    void clientErrorIsNotRetried();
    void serverErrorIsRetriedUntilItWorks();
    void serverErrorGivesUpAfterTheConfiguredAttempts();
    void refusedConnectionIsRetriedThenReported();
    void connectionClosedWithoutAnswerIsANetworkError();
    void silentServerTimesOut();

    void rateLimitIsReportedAndNotRetried_data();
    void rateLimitIsReportedAndNotRetried();
    void forbiddenWithoutRateLimitIsAnHttpError();
    void oversizedResponseIsRejected();

    void redirectWithinTheAllowList();
    void redirectToAnotherHostIsBlocked();
    void redirectToAnotherSchemeIsBlocked();
    void redirectLoopStops();

    void abortReportsExactlyOnce();
    void liveGitHub();
};

void GitHubClientTests::productionPolicy_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("allowed");

    QTest::newRow("registry file")
        << u"https://raw.githubusercontent.com/KERSEX/Registry/main/projects.json"_s << true;
    QTest::newRow("api") << u"https://api.github.com/repos/KERSEX/App/releases"_s << true;
    QTest::newRow("release download")
        << u"https://github.com/KERSEX/App/releases/download/v1.0.0/App.zip"_s << true;
    QTest::newRow("download redirect target")
        << u"https://objects.githubusercontent.com/github-production/abc"_s << true;
    QTest::newRow("new download redirect target")
        << u"https://release-assets.githubusercontent.com/abc"_s << true;
    QTest::newRow("uppercase host and scheme") << u"HTTPS://API.GITHUB.COM/x"_s << true;
    QTest::newRow("explicit default port") << u"https://api.github.com:443/x"_s << true;

    QTest::newRow("plain http") << u"http://raw.githubusercontent.com/x"_s << false;
    QTest::newRow("other host") << u"https://example.org/x"_s << false;
    QTest::newRow("allowed name as prefix of another host")
        << u"https://raw.githubusercontent.com.evil.example/x"_s << false;
    QTest::newRow("allowed name as suffix of another host")
        << u"https://evilraw.githubusercontent.com/x"_s << false;
    QTest::newRow("other github host") << u"https://gist.githubusercontent.com/x"_s << false;
    QTest::newRow("user info") << u"https://user:secret@api.github.com/x"_s << false;
    QTest::newRow("user name only") << u"https://api.github.com@example.org/x"_s << false;
    QTest::newRow("other port") << u"https://api.github.com:8443/x"_s << false;
    QTest::newRow("ftp") << u"ftp://api.github.com/x"_s << false;
    QTest::newRow("file") << u"file:///C:/Windows/win.ini"_s << false;
    QTest::newRow("trailing dot in host") << u"https://api.github.com./x"_s << false;
    QTest::newRow("ip address") << u"https://127.0.0.1/x"_s << false;
    QTest::newRow("no host") << u"https:///x"_s << false;
    QTest::newRow("relative") << u"/repos/a/b"_s << false;
    QTest::newRow("empty") << QString() << false;
}

void GitHubClientTests::productionPolicy()
{
    QFETCH(QString, url);
    QFETCH(bool, allowed);
    QCOMPARE(GitHubClient::Policy::production().allows(QUrl(url)), allowed);
}

void GitHubClientTests::blockedAddressFailsWithoutNetwork()
{
    TestHttpServer server([](const Request&) { return answer(200); });
    QVERIFY(server.listen());

    // The production policy does not allow plain HTTP or a local address.
    GitHubClient client(GitHubClient::Policy::production());
    const NetworkResult result = fetch(client, server.url(u"/x"_s));

    QCOMPARE(result.error, NetworkError::UrlNotAllowed);
    QCOMPARE(result.httpStatus, 0);
    QCOMPARE(server.requestCount(), 0);
}

void GitHubClientTests::finishedIsNeverEmittedBeforeGetReturns()
{
    // The caller connects to finished() after get() returns, so a request that
    // fails at once must not report from inside get().
    GitHubClient client(GitHubClient::Policy::production());
    bool emitted = false;
    GitHubRequest* request = client.get(QUrl(u"https://example.org/"_s));
    connect(request, &GitHubRequest::finished, this, [&emitted] { emitted = true; });
    QVERIFY(!emitted);
    QTRY_VERIFY_WITH_TIMEOUT(emitted, 5000);
}

void GitHubClientTests::successfulGet()
{
    TestHttpServer server([](const Request&) { return answer(200, R"({"ok":true})"); });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    const NetworkResult result = fetch(client, server.url(u"/data.json?x=1"_s));

    QVERIFY2(result.ok(), qPrintable(result.detail));
    QCOMPARE(result.httpStatus, 200);
    QCOMPARE(result.body, QByteArray(R"({"ok":true})"));
    QCOMPARE(result.attempts, 1);
    QCOMPARE(result.finalUrl.path(), u"/data.json"_s);
    QCOMPARE(server.requestCount(), 1);
    QCOMPARE(server.requests().first().method, QByteArray("GET"));
    QCOMPARE(server.requests().first().path, u"/data.json?x=1"_s);
}

void GitHubClientTests::requestIdentifiesTheLauncher()
{
    TestHttpServer server([](const Request&) { return answer(200); });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    FetchOptions options = quickOptions();
    options.accept = "application/vnd.github+json";
    QVERIFY(fetch(client, server.url(), options).ok());

    // GitHub rejects requests without a User-Agent.
    const Request& request = server.requests().first();
    const QByteArray expected =
        (launcher::branding::productName() + u'/' + launcher::branding::version()).toUtf8();
    QCOMPARE(request.headers.value("user-agent"), expected);
    QCOMPARE(request.headers.value("accept"), QByteArray("application/vnd.github+json"));
    // The API version header is meant for api.github.com only.
    QVERIFY(!request.headers.contains("x-github-api-version"));
    QVERIFY(!request.headers.contains("authorization"));
    QVERIFY(!request.headers.contains("cookie"));
}

void GitHubClientTests::rateLimitHeadersAreTracked()
{
    TestHttpServer server([](const Request&) {
        Response response = answer(200);
        response.addHeader("X-RateLimit-Limit", "60");
        response.addHeader("X-RateLimit-Remaining", "42");
        response.addHeader("X-RateLimit-Reset", "1893456000");
        return response;
    });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    QVERIFY(!client.rateLimit().isKnown());
    int notifications = 0;
    connect(&client, &GitHubClient::rateLimitChanged, this, [&notifications] { ++notifications; });

    const NetworkResult result = fetch(client, server.url());
    QVERIFY(result.ok());
    QVERIFY(result.rateLimit.isKnown());
    QCOMPARE(result.rateLimit.limit, 60);
    QCOMPARE(result.rateLimit.remaining, 42);
    QCOMPARE(result.rateLimit.resetAt.toSecsSinceEpoch(), 1893456000LL);

    QCOMPARE(client.rateLimit(), result.rateLimit);
    QCOMPARE(notifications, 1);

    // The same budget again is not news.
    QVERIFY(fetch(client, server.url()).ok());
    QCOMPARE(notifications, 1);
}

void GitHubClientTests::clientErrorIsNotRetried()
{
    TestHttpServer server([](const Request&) { return answer(404, "Not Found"); });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    const NetworkResult result = fetch(client, server.url());
    QCOMPARE(result.error, NetworkError::HttpError);
    QCOMPARE(result.httpStatus, 404);
    QCOMPARE(result.attempts, 1);
    QCOMPARE(server.requestCount(), 1);
}

void GitHubClientTests::serverErrorIsRetriedUntilItWorks()
{
    int calls = 0;
    TestHttpServer server(
        [&calls](const Request&) { return ++calls < 3 ? answer(503) : answer(200, "finally"); });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    const NetworkResult result = fetch(client, server.url());
    QVERIFY2(result.ok(), qPrintable(result.detail));
    QCOMPARE(result.body, QByteArray("finally"));
    QCOMPARE(result.attempts, 3);
    QCOMPARE(server.requestCount(), 3);
}

void GitHubClientTests::serverErrorGivesUpAfterTheConfiguredAttempts()
{
    TestHttpServer server([](const Request&) { return answer(500); });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    FetchOptions options = quickOptions();
    options.maxAttempts = 4;
    const NetworkResult result = fetch(client, server.url(), options);
    QCOMPARE(result.error, NetworkError::HttpError);
    QCOMPARE(result.httpStatus, 500);
    QCOMPARE(result.attempts, 4);
    QCOMPARE(server.requestCount(), 4);

    // One attempt means no retry at all.
    TestHttpServer once([](const Request&) { return answer(500); });
    QVERIFY(once.listen());
    options.maxAttempts = 1;
    QCOMPARE(fetch(client, once.url(), options).error, NetworkError::HttpError);
    QCOMPARE(once.requestCount(), 1);
}

void GitHubClientTests::refusedConnectionIsRetriedThenReported()
{
    // A port that was open a moment ago and is closed now.
    TestHttpServer server([](const Request&) { return answer(200); });
    QVERIFY(server.listen());
    const QUrl url = server.url();
    server.close();

    // Windows takes about two seconds to report a refused connection to a
    // closed local port, so depending on timing the attempt ends as a refusal
    // or as a timeout. Both mean "no connection" and are retried the same way.
    GitHubClient client(localPolicy());
    FetchOptions options = quickOptions();
    options.timeoutMs = 300;
    const NetworkResult result = fetch(client, url, options);
    QVERIFY2(result.error == NetworkError::Network || result.error == NetworkError::Timeout,
        qPrintable(result.detail));
    QCOMPARE(result.httpStatus, 0);
    QCOMPARE(result.attempts, 3);
    QVERIFY(!result.detail.isEmpty());
}

void GitHubClientTests::connectionClosedWithoutAnswerIsANetworkError()
{
    TestHttpServer server([](const Request&) {
        Response response;
        response.behavior = Response::Behavior::CloseWithoutAnswer;
        return response;
    });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    const NetworkResult result = fetch(client, server.url());
    QCOMPARE(result.error, NetworkError::Network);
    QCOMPARE(result.attempts, 3);
    // Qt itself sends a request once more when a connection is closed without
    // an answer, so the server may see more requests than there were attempts.
    QVERIFY(server.requestCount() >= 3);
}

void GitHubClientTests::silentServerTimesOut()
{
    TestHttpServer server([](const Request&) {
        Response response;
        response.behavior = Response::Behavior::Hang;
        return response;
    });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    FetchOptions options = quickOptions();
    options.timeoutMs = 300;
    options.maxAttempts = 1;

    QElapsedTimer clock;
    clock.start();
    const NetworkResult result = fetch(client, server.url(), options);
    QCOMPARE(result.error, NetworkError::Timeout);
    QVERIFY2(clock.elapsed() < 5000, "the timeout did not end the request");
}

void GitHubClientTests::rateLimitIsReportedAndNotRetried_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<QString>("headerName");
    QTest::addColumn<QString>("headerValue");

    QTest::newRow("429") << 429 << QString() << QString();
    QTest::newRow("429 with retry-after") << 429 << u"Retry-After"_s << u"30"_s;
    QTest::newRow("403 with budget used up") << 403 << u"X-RateLimit-Remaining"_s << u"0"_s;
    QTest::newRow("403 with retry-after") << 403 << u"Retry-After"_s << u"60"_s;
}

void GitHubClientTests::rateLimitIsReportedAndNotRetried()
{
    QFETCH(int, status);
    QFETCH(QString, headerName);
    QFETCH(QString, headerValue);

    TestHttpServer server([=](const Request&) {
        Response response = answer(status);
        response.addHeader("X-RateLimit-Limit", "60");
        if (headerName != u"X-RateLimit-Remaining"_s)
            response.addHeader("X-RateLimit-Remaining", "5");
        if (!headerName.isEmpty())
            response.addHeader(headerName.toUtf8(), headerValue.toUtf8());
        return response;
    });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    const NetworkResult result = fetch(client, server.url());
    QCOMPARE(result.error, NetworkError::RateLimited);
    QCOMPARE(result.httpStatus, status);
    // Trying again would only use up more of the budget.
    QCOMPARE(server.requestCount(), 1);
}

void GitHubClientTests::forbiddenWithoutRateLimitIsAnHttpError()
{
    TestHttpServer server([](const Request&) { return answer(403, "no access"); });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    const NetworkResult result = fetch(client, server.url());
    QCOMPARE(result.error, NetworkError::HttpError);
    QCOMPARE(result.httpStatus, 403);
    QCOMPARE(server.requestCount(), 1);
}

void GitHubClientTests::oversizedResponseIsRejected()
{
    TestHttpServer server([](const Request&) { return answer(200, QByteArray(200 * 1024, 'x')); });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    FetchOptions options = quickOptions();
    options.maxBytes = 1024;
    const NetworkResult result = fetch(client, server.url(), options);
    QCOMPARE(result.error, NetworkError::TooLarge);
    QVERIFY(result.body.isEmpty());
    QCOMPARE(server.requestCount(), 1);

    // The same response fits when the limit allows it.
    options.maxBytes = 1024 * 1024;
    const NetworkResult accepted = fetch(client, server.url(), options);
    QVERIFY2(accepted.ok(), qPrintable(accepted.detail));
    QCOMPARE(accepted.body.size(), 200 * 1024);
}

void GitHubClientTests::redirectWithinTheAllowList()
{
    quint16 port = 0;
    TestHttpServer server([&port](const Request& request) {
        if (request.path == u"/relative"_s)
            return redirect(u"/target"_s);
        if (request.path == u"/absolute"_s)
            return redirect(u"http://localhost:%1/target"_s.arg(port)); // another allowed host name
        if (request.path == u"/target"_s)
            return answer(200, "arrived");
        return answer(404);
    });
    QVERIFY(server.listen());
    port = server.port();

    GitHubClient client(localPolicy());
    const NetworkResult relative = fetch(client, server.url(u"/relative"_s));
    QVERIFY2(relative.ok(), qPrintable(relative.detail));
    QCOMPARE(relative.body, QByteArray("arrived"));
    QCOMPARE(relative.finalUrl.path(), u"/target"_s);
    QCOMPARE(relative.attempts, 1); // a redirect is no retry

    const NetworkResult absolute = fetch(client, server.url(u"/absolute"_s));
    QVERIFY2(absolute.ok(), qPrintable(absolute.detail));
    QCOMPARE(absolute.body, QByteArray("arrived"));
    QCOMPARE(absolute.finalUrl.host(), u"localhost"_s);
}

void GitHubClientTests::redirectToAnotherHostIsBlocked()
{
    TestHttpServer server([](const Request&) { return redirect(u"http://example.org/stolen"_s); });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    const NetworkResult result = fetch(client, server.url());
    QCOMPARE(result.error, NetworkError::UrlNotAllowed);
    QVERIFY(result.detail.contains(u"example.org"_s));
    QVERIFY(result.body.isEmpty());
    QCOMPARE(server.requestCount(), 1);
}

void GitHubClientTests::redirectToAnotherSchemeIsBlocked()
{
    // Only http is allowed by this client: sending the request on to https is as
    // much a violation as the other way round would be for the production policy.
    TestHttpServer server([](const Request& request) {
        const QString host = QString::fromUtf8(request.headers.value("host"));
        return redirect(u"https://%1/secure"_s.arg(host));
    });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    const NetworkResult result = fetch(client, server.url());
    QCOMPARE(result.error, NetworkError::UrlNotAllowed);
    QCOMPARE(server.requestCount(), 1);
}

void GitHubClientTests::redirectLoopStops()
{
    TestHttpServer server([](const Request&) { return redirect(u"/loop"_s); });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    FetchOptions options = quickOptions();
    options.maxRedirects = 3;
    const NetworkResult result = fetch(client, server.url(u"/loop"_s), options);
    QCOMPARE(result.error, NetworkError::TooManyRedirects);
    QCOMPARE(server.requestCount(), 4); // the first request and three followed redirects
}

void GitHubClientTests::abortReportsExactlyOnce()
{
    TestHttpServer server([](const Request&) {
        Response response;
        response.behavior = Response::Behavior::Hang;
        return response;
    });
    QVERIFY(server.listen());

    GitHubClient client(localPolicy());
    QPointer<GitHubRequest> request = client.get(server.url(), quickOptions());
    int reports = 0;
    NetworkError error = NetworkError::None;
    connect(request, &GitHubRequest::finished, this, [&](const NetworkResult& result) {
        ++reports;
        error = result.error;
    });

    QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 5000); // the request is under way
    QCOMPARE(reports, 0);

    request->abort();
    QCOMPARE(reports, 1);
    QCOMPARE(error, NetworkError::Aborted);

    // Neither a second abort nor the passing of time reports again.
    if (request)
        request->abort();
    QTest::qWait(200);
    QCOMPARE(reports, 1);
    QCOMPARE(server.requestCount(), 1); // no retry after an abort
}

void GitHubClientTests::liveGitHub()
{
    if (!qEnvironmentVariableIsSet("LAUNCHER_LIVE_TESTS"))
        QSKIP("Set LAUNCHER_LIVE_TESTS=1 to run the checks against the real GitHub.");

    // Proves that TLS works in this build: the production policy only allows HTTPS.
    GitHubClient client(GitHubClient::Policy::production());
    const NetworkResult api =
        fetch(client, QUrl(u"https://api.github.com/repos/KERSEX/KERJAMI_Launcher"_s), {});
    QVERIFY2(api.ok(), qPrintable(api.detail));
    const QJsonObject repository = QJsonDocument::fromJson(api.body).object();
    QCOMPARE(repository.value("name"_L1).toString(), u"KERJAMI_Launcher"_s);
    QVERIFY(client.rateLimit().isKnown());
    qInfo() << "Request budget:" << client.rateLimit().remaining << "of" << client.rateLimit().limit;

    // The registry may or may not exist yet; both answers prove that the connection works.
    const NetworkResult registry = fetch(client, launcher::branding::registryUrl(), {});
    qInfo() << "Registry:" << (registry.ok() ? u"HTTP 200"_s : u"HTTP %1"_s.arg(registry.httpStatus));
    QVERIFY2(registry.ok() || registry.error == NetworkError::HttpError, qPrintable(registry.detail));
}

QTEST_GUILESS_MAIN(GitHubClientTests)
#include "GitHubClientTests.moc"
