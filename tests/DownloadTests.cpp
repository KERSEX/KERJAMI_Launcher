// Downloads: the speed calculation, the text for sizes and durations, and the
// DownloadManager with its queue. A server on the local machine plays GitHub.

#include "TestHttpServer.h"
#include "core/Format.h"
#include "downloads/DownloadManager.h"
#include "downloads/TransferRate.h"
#include "github/GitHubClient.h"
#include "github/ReleaseParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using launcher::Download;
using launcher::DownloadId;
using launcher::DownloadManager;
using launcher::DownloadProblem;
using launcher::DownloadRequest;
using launcher::DownloadState;
using launcher::FetchOptions;
using launcher::GitHubClient;
using launcher::TransferRate;
using Response = TestHttpServer::Response;
using Request = TestHttpServer::Request;
namespace format = launcher::format;

namespace {

/// Bytes that are not all the same, so that a file with wrong, shifted or doubled content differs.
QByteArray payload(qint64 size)
{
    QByteArray data(size, '\0');
    for (qint64 i = 0; i < size; ++i)
        data[i] = static_cast<char>((i * 31 + 7) & 0xFF);
    return data;
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

Response answer(int status, const QByteArray& body = {})
{
    Response response;
    response.status = status;
    response.body = body;
    return response;
}

Response partialThenHang(const QByteArray& body, qint64 partial)
{
    Response response = answer(200, body);
    response.behavior = Response::Behavior::SendPartialThenHang;
    response.partialBytes = partial;
    return response;
}

} // namespace

class DownloadTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // Speed, text
    void rateIsZeroWithoutMeasurement();
    void rateOfASteadyTransfer();
    void rateIsSmoothed();
    void rateCollectsShortIntervals();
    void rateRestartsWhenTheTransferBeginsAgain();
    void remainingTime_data();
    void remainingTime();
    void sizesAreWrittenWithTheLocalSeparator();
    void durationsAreWrittenInTheLargestUnits();

    // Manager: the normal way
    void downloadIsMovedIntoPlace();
    void progressIsReportedUpToTheFullSize();
    void redirectedDownloadIsStoredWithoutTheRedirectPage();
    void interruptedTransferIsRetriedFromTheStart();
    void existingFileIsReplaced();
    void sameFileForTheSameProjectIsNotStartedTwice();

    // Manager: when something goes wrong
    void wrongSizeFails();
    void moreDataThanAnnouncedFails();
    void httpErrorFails();
    void rateLimitFails();
    void noConnectionFails();
    void notEnoughSpaceFailsBeforeAnyRequest();
    void blockedFolderFails();
    void targetThatCannotBeReplacedFails();
    void invalidRequestsFailAtOnce_data();
    void invalidRequestsFailAtOnce();

    // Manager: queue, cancel, housekeeping
    void queueRunsOnlyAFewAtATime();
    void queueKeepsTheOrder();
    void runningDownloadCanBeCancelled();
    void queuedDownloadCanBeCancelled();
    void cancelledDownloadReportsNoFailure();
    void clearFinishedKeepsActiveDownloads();
    void historyIsLimited();
    void leftoverPartialFilesAreRemovedAtStart();
    void destroyingTheManagerStopsTransfers();

private:
    std::unique_ptr<DownloadManager> makeManager()
    {
        auto manager = std::make_unique<DownloadManager>(*m_client, m_tempDir, m_downloadsDir);
        FetchOptions options;
        options.accept = "application/octet-stream";
        options.retryDelayMs = 10;
        options.timeoutMs = 2000;
        options.maxAttempts = 3;
        manager->setFetchOptions(options);
        return manager;
    }

    DownloadRequest request(const QString& path = u"/file"_s, const QString& fileName = u"Demo-1.0.0.zip"_s,
        qint64 size = -1) const
    {
        DownloadRequest result;
        result.projectId = u"demo"_s;
        result.title = u"Demo 1.0.0"_s;
        result.url = m_server->url(path);
        result.fileName = fileName;
        result.expectedSize = size >= 0 ? size : m_payload.size();
        return result;
    }

    QStringList partialFiles() const
    {
        return QDir(m_tempDir).entryList({u"download-*.part"_s}, QDir::Files);
    }
    QString finalFile(const QString& fileName = u"Demo-1.0.0.zip"_s) const
    {
        return m_downloadsDir + u"/demo/"_s + fileName;
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_tempDir;
    QString m_downloadsDir;
    QByteArray m_payload;
    std::unique_ptr<GitHubClient> m_client;
    std::unique_ptr<TestHttpServer> m_server;
    std::function<Response(const Request&)> m_script;
};

void DownloadTests::initTestCase()
{
    // Texts are compared as the German user interface shows them.
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
}

void DownloadTests::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_tempDir = m_dir->filePath(u"temp"_s);
    m_downloadsDir = m_dir->filePath(u"downloads"_s);
    QVERIFY(QDir().mkpath(m_tempDir));
    QVERIFY(QDir().mkpath(m_downloadsDir));

    m_payload = payload(300 * 1024);
    m_client = std::make_unique<GitHubClient>(GitHubClient::Policy {{u"http"_s}, {u"127.0.0.1"_s}, true});
    m_script = [this](const Request&) { return answer(200, m_payload); };
    m_server = std::make_unique<TestHttpServer>([this](const Request& request) { return m_script(request); });
    QVERIFY(m_server->listen());
}

void DownloadTests::cleanup()
{
    m_server.reset();
    m_client.reset();
    m_dir.reset();
}

// ---- Speed and text ----------------------------------------------------------

void DownloadTests::rateIsZeroWithoutMeasurement()
{
    TransferRate rate;
    QCOMPARE(rate.bytesPerSecond(), 0LL);
    rate.addSample(0, 0);
    QCOMPARE(rate.bytesPerSecond(), 0LL); // the first sample is only the starting point
}

void DownloadTests::rateOfASteadyTransfer()
{
    TransferRate rate;
    for (int second = 0; second <= 5; ++second)
        rate.addSample(second * 1000, second * 4000);
    QCOMPARE(rate.bytesPerSecond(), 4000LL);
}

void DownloadTests::rateIsSmoothed()
{
    TransferRate rate;
    rate.addSample(0, 0);
    rate.addSample(1000, 1000); // 1000 B/s
    QCOMPARE(rate.bytesPerSecond(), 1000LL);

    rate.addSample(2000, 1000 + 10000); // a burst of 10000 B/s
    // 70 % of the old average and 30 % of the new measurement.
    QCOMPARE(rate.bytesPerSecond(), 3700LL);

    rate.addSample(3000, 11000 + 10000);
    QVERIFY(rate.bytesPerSecond() > 3700);
    QVERIFY(rate.bytesPerSecond() < 10000);
}

void DownloadTests::rateCollectsShortIntervals()
{
    // Many samples closer together than the minimum interval are summed up, not lost.
    TransferRate rate;
    for (int step = 0; step <= 10; ++step)
        rate.addSample(step * 100, step * 100); // 1000 B/s, one sample every 100 ms
    QCOMPARE(rate.bytesPerSecond(), 1000LL);
}

void DownloadTests::rateRestartsWhenTheTransferBeginsAgain()
{
    TransferRate rate;
    rate.addSample(0, 0);
    rate.addSample(1000, 50000);
    QCOMPARE(rate.bytesPerSecond(), 50000LL);

    // After an interrupted transfer the byte count starts from zero again.
    rate.addSample(1500, 0);
    QCOMPARE(rate.bytesPerSecond(), 0LL);
    rate.addSample(2500, 2000);
    QCOMPARE(rate.bytesPerSecond(), 2000LL);

    rate.reset();
    QCOMPARE(rate.bytesPerSecond(), 0LL);
}

void DownloadTests::remainingTime_data()
{
    QTest::addColumn<int>("state");
    QTest::addColumn<qint64>("received");
    QTest::addColumn<qint64>("total");
    QTest::addColumn<qint64>("rate");
    QTest::addColumn<qint64>("expected");

    const int running = static_cast<int>(DownloadState::Running);
    QTest::newRow("six seconds") << running << 4000LL << 10000LL << 1000LL << 6LL;
    QTest::newRow("rounded up") << running << 9999LL << 10000LL << 3000LL << 1LL;
    QTest::newRow("unknown speed") << running << 4000LL << 10000LL << 0LL << -1LL;
    QTest::newRow("already complete") << running << 10000LL << 10000LL << 1000LL << -1LL;
    QTest::newRow("queued") << static_cast<int>(DownloadState::Queued) << 0LL << 10000LL << 1000LL << -1LL;
    QTest::newRow("finished") << static_cast<int>(DownloadState::Finished) << 5000LL << 10000LL << 1000LL
                              << -1LL;
}

void DownloadTests::remainingTime()
{
    QFETCH(int, state);
    QFETCH(qint64, received);
    QFETCH(qint64, total);
    QFETCH(qint64, rate);
    QFETCH(qint64, expected);

    Download download;
    download.state = static_cast<DownloadState>(state);
    download.bytesReceived = received;
    download.bytesTotal = total;
    download.bytesPerSecond = rate;
    QCOMPARE(download.remainingSeconds(), expected);
}

void DownloadTests::sizesAreWrittenWithTheLocalSeparator()
{
    QCOMPARE(format::bytes(1536), u"1,5 kB"_s); // Qt writes the traditional kilo with a small k
    QCOMPARE(format::bytes(5 * 1024 * 1024), u"5,0 MB"_s);
    QCOMPARE(format::bytes(3LL * 1024 * 1024 * 1024 / 2), u"1,5 GB"_s);
    QCOMPARE(format::speed(3 * 1024 * 1024), u"3,0 MB/s"_s);
    QVERIFY(format::bytes(0).contains(u"0"_s));
}

void DownloadTests::durationsAreWrittenInTheLargestUnits()
{
    QCOMPARE(format::duration(0), u"0 s"_s);
    QCOMPARE(format::duration(59), u"59 s"_s);
    QCOMPARE(format::duration(60), u"1 min 0 s"_s);
    QCOMPARE(format::duration(185), u"3 min 5 s"_s);
    QCOMPARE(format::duration(3599), u"59 min 59 s"_s);
    QCOMPARE(format::duration(3600), u"1 h 0 min"_s);
    QCOMPARE(format::duration(3840), u"1 h 4 min"_s);
    QCOMPARE(format::duration(-1), QString());
}

// ---- Manager: the normal way -------------------------------------------------

void DownloadTests::downloadIsMovedIntoPlace()
{
    auto manager = makeManager();
    QSignalSpy added(manager.get(), &DownloadManager::added);
    QSignalSpy states(manager.get(), &DownloadManager::stateChanged);
    QSignalSpy finished(manager.get(), &DownloadManager::finished);
    QSignalSpy failed(manager.get(), &DownloadManager::failed);

    const DownloadId id = manager->enqueue(request());
    QCOMPARE(added.count(), 1);
    QVERIFY(manager->download(id)->isActive());
    QCOMPARE(manager->activeCount(), 1);

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(finished.at(0).at(0).toULongLong(), id);
    QVERIFY(states.count() >= 2); // started, then finished

    const std::optional<Download> download = manager->download(id);
    QVERIFY(download.has_value());
    QCOMPARE(download->state, DownloadState::Finished);
    QCOMPARE(download->problem, DownloadProblem::None);
    QCOMPARE(download->bytesReceived, static_cast<qint64>(m_payload.size()));
    QVERIFY(download->finishedAt.isValid());
    QCOMPARE(download->bytesPerSecond, 0LL);
    QCOMPARE(manager->activeCount(), 0);

    // The file is complete, in its place, and nothing half-finished is left.
    QCOMPARE(download->filePath, finalFile());
    QCOMPARE(readFile(download->filePath), m_payload);
    QVERIFY(partialFiles().isEmpty());

    QCOMPARE(m_server->requestCount(), 1);
    QCOMPARE(m_server->requests().first().headers.value("accept"), QByteArray("application/octet-stream"));
}

void DownloadTests::progressIsReportedUpToTheFullSize()
{
    m_payload = payload(3 * 1024 * 1024);
    auto manager = makeManager();

    QList<qint64> seen;
    const DownloadId id = manager->enqueue(request());
    connect(manager.get(), &DownloadManager::progressChanged, this,
        [&](quint64 changed) { seen.append(manager->download(changed)->bytesReceived); });

    QTRY_COMPARE_WITH_TIMEOUT(manager->download(id)->state, DownloadState::Finished, 15000);

    QVERIFY(!seen.isEmpty());
    for (qsizetype i = 1; i < seen.size(); ++i)
        QVERIFY2(seen.at(i) >= seen.at(i - 1), "progress went backwards");
    QCOMPARE(seen.last(), static_cast<qint64>(m_payload.size()));
    QCOMPARE(manager->download(id)->bytesTotal, static_cast<qint64>(m_payload.size()));
    QCOMPARE(readFile(finalFile()), m_payload);
}

void DownloadTests::redirectedDownloadIsStoredWithoutTheRedirectPage()
{
    m_script = [this](const Request& request) {
        if (request.path == u"/file"_s) {
            Response response = answer(302, "Moved to /real, this page must not end up in the file");
            response.addHeader("Location", "/real");
            return response;
        }
        return answer(200, m_payload);
    };

    auto manager = makeManager();
    const DownloadId id = manager->enqueue(request());
    QTRY_COMPARE_WITH_TIMEOUT(manager->download(id)->state, DownloadState::Finished, 10000);
    QCOMPARE(readFile(finalFile()), m_payload);
    QCOMPARE(m_server->requestCount(), 2);
}

void DownloadTests::interruptedTransferIsRetriedFromTheStart()
{
    int calls = 0;
    m_script = [&](const Request&) {
        if (++calls == 1) {
            Response response = answer(200, m_payload);
            response.behavior = Response::Behavior::SendPartialThenClose;
            response.partialBytes = 100 * 1024;
            return response;
        }
        return answer(200, m_payload);
    };

    auto manager = makeManager();
    const DownloadId id = manager->enqueue(request());
    QTRY_COMPARE_WITH_TIMEOUT(manager->download(id)->state, DownloadState::Finished, 15000);

    // The second attempt starts the file anew: not 100 KB of the first try plus the whole second.
    QCOMPARE(readFile(finalFile()), m_payload);
    QCOMPARE(m_server->requestCount(), 2);
}

void DownloadTests::existingFileIsReplaced()
{
    QVERIFY(writeFile(finalFile(), "an older version"));

    auto manager = makeManager();
    const DownloadId id = manager->enqueue(request());
    QTRY_COMPARE_WITH_TIMEOUT(manager->download(id)->state, DownloadState::Finished, 10000);
    QCOMPARE(readFile(finalFile()), m_payload);
}

void DownloadTests::sameFileForTheSameProjectIsNotStartedTwice()
{
    m_script = [this](const Request&) { return partialThenHang(m_payload, 1000); };
    auto manager = makeManager();

    const DownloadId first = manager->enqueue(request());
    const DownloadId second = manager->enqueue(request());
    QCOMPARE(second, first);
    QCOMPARE(manager->downloads().size(), 1);

    // Another file of the same project is no duplicate.
    const DownloadId other = manager->enqueue(request(u"/other"_s, u"Other.zip"_s));
    QVERIFY(other != first);
    QCOMPARE(manager->downloads().size(), 2);

    // Once the first one is over, the same file can be fetched again.
    manager->cancel(first);
    const DownloadId again = manager->enqueue(request());
    QVERIFY(again != first);
}

// ---- Manager: when something goes wrong --------------------------------------

void DownloadTests::wrongSizeFails()
{
    // The release announced more than the server has.
    auto manager = makeManager();
    const DownloadId id = manager->enqueue(request(u"/file"_s, u"Demo-1.0.0.zip"_s, m_payload.size() + 10));
    QTRY_COMPARE_WITH_TIMEOUT(manager->download(id)->state, DownloadState::Failed, 10000);

    const Download download = *manager->download(id);
    QCOMPARE(download.problem, DownloadProblem::SizeMismatch);
    QVERIFY(!QFileInfo::exists(finalFile()));
    QVERIFY(partialFiles().isEmpty());
}

void DownloadTests::moreDataThanAnnouncedFails()
{
    m_payload = payload(5000);
    auto manager = makeManager();
    const DownloadId id = manager->enqueue(request(u"/file"_s, u"Demo-1.0.0.zip"_s, 1000));
    QTRY_COMPARE_WITH_TIMEOUT(manager->download(id)->state, DownloadState::Failed, 10000);

    QCOMPARE(manager->download(id)->problem, DownloadProblem::TooLarge);
    QVERIFY(!QFileInfo::exists(finalFile()));
    QVERIFY(partialFiles().isEmpty());
}

void DownloadTests::httpErrorFails()
{
    m_script = [](const Request&) { return answer(404, "Not Found"); };
    auto manager = makeManager();
    const DownloadId id = manager->enqueue(request());
    QTRY_COMPARE_WITH_TIMEOUT(manager->download(id)->state, DownloadState::Failed, 10000);

    const Download download = *manager->download(id);
    QCOMPARE(download.problem, DownloadProblem::Http);
    QCOMPARE(download.httpStatus, 404);
    QVERIFY(!QFileInfo::exists(finalFile()));
    QVERIFY(partialFiles().isEmpty());
    QCOMPARE(m_server->requestCount(), 1); // a missing file is not asked for again
}

void DownloadTests::rateLimitFails()
{
    m_script = [](const Request&) {
        Response response = answer(429, "slow down");
        response.addHeader("Retry-After", "60");
        return response;
    };
    auto manager = makeManager();
    const DownloadId id = manager->enqueue(request());
    QTRY_COMPARE_WITH_TIMEOUT(manager->download(id)->state, DownloadState::Failed, 10000);
    QCOMPARE(manager->download(id)->problem, DownloadProblem::RateLimited);
    QCOMPARE(m_server->requestCount(), 1);
}

void DownloadTests::noConnectionFails()
{
    const DownloadRequest closed = request();
    m_server->close();

    auto manager = makeManager();
    FetchOptions options;
    options.timeoutMs = 500; // a closed local port takes about two seconds to refuse on Windows
    options.maxAttempts = 1;
    manager->setFetchOptions(options);

    const DownloadId id = manager->enqueue(closed);
    QTRY_COMPARE_WITH_TIMEOUT(manager->download(id)->state, DownloadState::Failed, 10000);
    QCOMPARE(manager->download(id)->problem, DownloadProblem::Unreachable);
    QVERIFY(partialFiles().isEmpty());
}

void DownloadTests::notEnoughSpaceFailsBeforeAnyRequest()
{
    auto manager = makeManager();
    manager->setAvailableSpaceFunction([](const QString&) { return 1024LL * 1024; });

    const DownloadId id = manager->enqueue(request());
    // Refused at the start, which is why it is already over.
    QCOMPARE(manager->download(id)->state, DownloadState::Failed);
    QCOMPARE(manager->download(id)->problem, DownloadProblem::InsufficientSpace);
    QCOMPARE(m_server->requestCount(), 0);

    // The file itself fits but not the reserve on top of it.
    manager->setAvailableSpaceFunction(
        [this](const QString&) { return static_cast<qint64>(m_payload.size()) + 1000; });
    QCOMPARE(manager->download(manager->enqueue(request(u"/file"_s, u"B.zip"_s)))->problem,
        DownloadProblem::InsufficientSpace);

    // Plenty of room, or a drive that cannot tell: the download goes ahead.
    manager->setAvailableSpaceFunction([](const QString&) { return 100LL * 1024 * 1024 * 1024; });
    const DownloadId fine = manager->enqueue(request(u"/file"_s, u"C.zip"_s));
    QTRY_COMPARE_WITH_TIMEOUT(manager->download(fine)->state, DownloadState::Finished, 10000);
    manager->setAvailableSpaceFunction([](const QString&) { return -1LL; });
    const DownloadId unknown = manager->enqueue(request(u"/file"_s, u"D.zip"_s));
    QTRY_COMPARE_WITH_TIMEOUT(manager->download(unknown)->state, DownloadState::Finished, 10000);
}

void DownloadTests::blockedFolderFails()
{
    // A file where the project's folder should be.
    QVERIFY(writeFile(m_downloadsDir + u"/demo"_s, "not a folder"));

    auto manager = makeManager();
    const DownloadId id = manager->enqueue(request());
    QCOMPARE(manager->download(id)->state, DownloadState::Failed);
    QCOMPARE(manager->download(id)->problem, DownloadProblem::StorageError);
    QCOMPARE(m_server->requestCount(), 0);
}

void DownloadTests::targetThatCannotBeReplacedFails()
{
    // A folder in the place of the file.
    QVERIFY(writeFile(finalFile() + u"/inside.txt"_s, "x"));

    auto manager = makeManager();
    const DownloadId id = manager->enqueue(request());
    QTRY_COMPARE_WITH_TIMEOUT(manager->download(id)->state, DownloadState::Failed, 10000);
    QCOMPARE(manager->download(id)->problem, DownloadProblem::StorageError);
    QVERIFY(partialFiles().isEmpty());
    QVERIFY(QFileInfo(finalFile()).isDir()); // what was there is untouched
}

void DownloadTests::invalidRequestsFailAtOnce_data()
{
    QTest::addColumn<QString>("projectId");
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<qint64>("size");
    QTest::addColumn<QString>("url"); // empty: the local server

    const QString ok = u"demo"_s;
    const QString file = u"Demo.zip"_s;
    QTest::newRow("project id with capital") << u"Demo"_s << file << 1000LL << QString();
    QTest::newRow("project id with path") << u"../demo"_s << file << 1000LL << QString();
    QTest::newRow("project id empty") << QString() << file << 1000LL << QString();
    QTest::newRow("file name with parent folder") << ok << u"../Demo.zip"_s << 1000LL << QString();
    QTest::newRow("file name with folder") << ok << u"a/Demo.zip"_s << 1000LL << QString();
    QTest::newRow("file name is a device") << ok << u"con.zip"_s << 1000LL << QString();
    QTest::newRow("file name empty") << ok << QString() << 1000LL << QString();
    QTest::newRow("size zero") << ok << file << 0LL << QString();
    QTest::newRow("size negative") << ok << file << -5LL << QString();
    QTest::newRow("size above the limit")
        << ok << file << launcher::ReleaseParser::kMaxAssetBytes + 1 << QString();
    QTest::newRow("address of another host") << ok << file << 1000LL << u"http://example.org/Demo.zip"_s;
    QTest::newRow("address of another scheme") << ok << file << 1000LL << u"https://127.0.0.1/Demo.zip"_s;
    QTest::newRow("local file") << ok << file << 1000LL << u"file:///C:/Windows/win.ini"_s;
    QTest::newRow("address with credentials")
        << ok << file << 1000LL << u"http://user:pw@127.0.0.1/Demo.zip"_s;
}

void DownloadTests::invalidRequestsFailAtOnce()
{
    QFETCH(QString, projectId);
    QFETCH(QString, fileName);
    QFETCH(qint64, size);
    QFETCH(QString, url);

    auto manager = makeManager();
    QSignalSpy added(manager.get(), &DownloadManager::added);
    QSignalSpy failed(manager.get(), &DownloadManager::failed);

    DownloadRequest bad = request();
    bad.projectId = projectId;
    bad.fileName = fileName;
    bad.expectedSize = size;
    if (!url.isEmpty())
        bad.url = QUrl(url);

    const DownloadId id = manager->enqueue(bad);
    const Download download = *manager->download(id);
    QCOMPARE(download.state, DownloadState::Failed);
    QCOMPARE(download.problem, DownloadProblem::InvalidRequest);
    QVERIFY(!download.detail.isEmpty());
    QCOMPARE(added.count(), 1);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(m_server->requestCount(), 0);
    QCOMPARE(manager->activeCount(), 0);
}

// ---- Manager: queue, cancel, housekeeping ------------------------------------

void DownloadTests::queueRunsOnlyAFewAtATime()
{
    m_script = [this](const Request&) { return partialThenHang(m_payload, 1000); };
    auto manager = makeManager();
    manager->setMaxParallel(2);

    const DownloadId first = manager->enqueue(request(u"/a"_s, u"A.zip"_s));
    const DownloadId second = manager->enqueue(request(u"/b"_s, u"B.zip"_s));
    const DownloadId third = manager->enqueue(request(u"/c"_s, u"C.zip"_s));

    QTRY_COMPARE_WITH_TIMEOUT(m_server->requestCount(), 2, 5000);
    QTest::qWait(200);
    QCOMPARE(m_server->requestCount(), 2); // the third waits
    QCOMPARE(manager->download(first)->state, DownloadState::Running);
    QCOMPARE(manager->download(second)->state, DownloadState::Running);
    QCOMPARE(manager->download(third)->state, DownloadState::Queued);
    QCOMPARE(manager->activeCount(), 3);

    // A free place goes to the one that has waited.
    manager->cancel(first);
    QTRY_COMPARE_WITH_TIMEOUT(m_server->requestCount(), 3, 5000);
    QCOMPARE(manager->download(third)->state, DownloadState::Running);
}

void DownloadTests::queueKeepsTheOrder()
{
    auto manager = makeManager();
    manager->setMaxParallel(1);

    const DownloadId a = manager->enqueue(request(u"/a"_s, u"A.zip"_s));
    const DownloadId b = manager->enqueue(request(u"/b"_s, u"B.zip"_s));
    const DownloadId c = manager->enqueue(request(u"/c"_s, u"C.zip"_s));
    QTRY_COMPARE_WITH_TIMEOUT(manager->activeCount(), 0, 15000);

    for (const DownloadId id : {a, b, c})
        QCOMPARE(manager->download(id)->state, DownloadState::Finished);
    QCOMPARE(m_server->requestCount(), 3);
    QCOMPARE(m_server->requests().at(0).path, u"/a"_s);
    QCOMPARE(m_server->requests().at(1).path, u"/b"_s);
    QCOMPARE(m_server->requests().at(2).path, u"/c"_s);
    QCOMPARE(manager->downloads().size(), 3);
}

void DownloadTests::runningDownloadCanBeCancelled()
{
    m_script = [this](const Request&) { return partialThenHang(m_payload, 100 * 1024); };
    auto manager = makeManager();

    QElapsedTimer clock;
    clock.start();
    const DownloadId id = manager->enqueue(request());
    // Wait until data is really coming in and a partial file exists.
    QTRY_VERIFY_WITH_TIMEOUT(manager->download(id)->bytesReceived > 0, 10000);
    // Progress follows the data at once, it does not wait for a timeout.
    QVERIFY2(clock.elapsed() < 1000, "the progress came in late");
    QVERIFY(!partialFiles().isEmpty());

    manager->cancel(id);
    const Download download = *manager->download(id);
    QCOMPARE(download.state, DownloadState::Cancelled);
    QVERIFY(download.finishedAt.isValid());
    QVERIFY(!download.isActive());
    QVERIFY(partialFiles().isEmpty());
    QVERIFY(!QFileInfo::exists(finalFile()));

    // Cancelling something that is over does nothing.
    manager->cancel(id);
    manager->cancel(9999);
    QCOMPARE(manager->download(id)->state, DownloadState::Cancelled);
}

void DownloadTests::queuedDownloadCanBeCancelled()
{
    m_script = [this](const Request&) { return partialThenHang(m_payload, 1000); };
    auto manager = makeManager();
    manager->setMaxParallel(1);

    const DownloadId running = manager->enqueue(request(u"/a"_s, u"A.zip"_s));
    const DownloadId waiting = manager->enqueue(request(u"/b"_s, u"B.zip"_s));
    QTRY_COMPARE_WITH_TIMEOUT(m_server->requestCount(), 1, 5000);
    QCOMPARE(manager->download(waiting)->state, DownloadState::Queued);

    manager->cancel(waiting);
    QCOMPARE(manager->download(waiting)->state, DownloadState::Cancelled);
    QCOMPARE(manager->download(running)->state, DownloadState::Running);

    // When the running one ends, the cancelled one must not wake up.
    manager->cancel(running);
    QTest::qWait(200);
    QCOMPARE(m_server->requestCount(), 1);
}

void DownloadTests::cancelledDownloadReportsNoFailure()
{
    m_script = [this](const Request&) { return partialThenHang(m_payload, 50 * 1024); };
    auto manager = makeManager();
    QSignalSpy finished(manager.get(), &DownloadManager::finished);
    QSignalSpy failed(manager.get(), &DownloadManager::failed);
    QSignalSpy states(manager.get(), &DownloadManager::stateChanged);

    const DownloadId id = manager->enqueue(request());
    QTRY_VERIFY_WITH_TIMEOUT(manager->download(id)->bytesReceived > 0, 10000);
    const int before = static_cast<int>(states.count());
    manager->cancel(id);
    QTest::qWait(200);

    // The user's own decision is neither a success nor an error.
    QCOMPARE(finished.count(), 0);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(states.count(), before + 1);
}

void DownloadTests::clearFinishedKeepsActiveDownloads()
{
    m_script = [this](const Request& request) {
        if (request.path == u"/slow"_s)
            return partialThenHang(m_payload, 1000);
        if (request.path == u"/missing"_s)
            return answer(404);
        return answer(200, m_payload);
    };
    auto manager = makeManager();
    manager->setMaxParallel(3);

    const DownloadId good = manager->enqueue(request(u"/ok"_s, u"Good.zip"_s));
    const DownloadId bad = manager->enqueue(request(u"/missing"_s, u"Bad.zip"_s));
    const DownloadId slow = manager->enqueue(request(u"/slow"_s, u"Slow.zip"_s));
    QTRY_COMPARE_WITH_TIMEOUT(manager->download(good)->state, DownloadState::Finished, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(manager->download(bad)->state, DownloadState::Failed, 10000);

    QSignalSpy removed(manager.get(), &DownloadManager::removed);
    manager->clearFinished();

    QCOMPARE(removed.count(), 2);
    QCOMPARE(manager->downloads().size(), 1);
    QCOMPARE(manager->downloads().first().id, slow);
    QVERIFY(!manager->download(good).has_value());
    QVERIFY(manager->download(slow)->isActive());
    // Forgetting an entry does not delete the file.
    QVERIFY(QFileInfo::exists(finalFile(u"Good.zip"_s)));
}

void DownloadTests::historyIsLimited()
{
    auto manager = makeManager();
    QSignalSpy removed(manager.get(), &DownloadManager::removed);

    DownloadRequest bad = request();
    bad.projectId = u"Bad Id"_s; // fails at once
    const int total = DownloadManager::kMaxKeptFinished + 5;
    for (int i = 0; i < total; ++i)
        manager->enqueue(bad);

    QCOMPARE(manager->downloads().size(), DownloadManager::kMaxKeptFinished);
    QCOMPARE(removed.count(), 5);
    // The oldest entries are the ones that go.
    QCOMPARE(manager->downloads().first().id, 6ULL);
    QCOMPARE(removed.at(0).at(0).toULongLong(), 1ULL);
}

void DownloadTests::leftoverPartialFilesAreRemovedAtStart()
{
    QVERIFY(writeFile(m_tempDir + u"/download-7.part"_s, "half a file"));
    QVERIFY(writeFile(m_tempDir + u"/download-8.part"_s, "another one"));
    QVERIFY(writeFile(m_tempDir + u"/keep.txt"_s, "not ours"));
    QVERIFY(writeFile(m_tempDir + u"/staging-demo/data.bin"_s, "installer staging"));

    auto manager = makeManager();
    QVERIFY(partialFiles().isEmpty());
    QVERIFY(QFileInfo::exists(m_tempDir + u"/keep.txt"_s));
    QVERIFY(QFileInfo::exists(m_tempDir + u"/staging-demo/data.bin"_s));
}

void DownloadTests::destroyingTheManagerStopsTransfers()
{
    m_script = [this](const Request&) { return partialThenHang(m_payload, 100 * 1024); };
    auto manager = makeManager();
    const DownloadId id = manager->enqueue(request());
    QTRY_VERIFY_WITH_TIMEOUT(manager->download(id)->bytesReceived > 0, 10000);
    QVERIFY(!partialFiles().isEmpty());

    manager.reset();
    QVERIFY(partialFiles().isEmpty());
    QVERIFY(!QFileInfo::exists(finalFile()));
}

QTEST_GUILESS_MAIN(DownloadTests)
#include "DownloadTests.moc"
