// The launcher's own update: finding a newer release, downloading and checking
// it, the hostile packages it must refuse, and the hand-over to the update run.
// A server on the local machine plays GitHub; starting the update run is replaced
// by a function that records what it was asked to start (except in the one test
// that runs the whole chain with the real program).

#include "SleepyProject.h"
#include "TestHttpServer.h"
#include "ZipBuilder.h"
#include "app/Branding.h"
#include "core/JsonFile.h"
#include "core/PathManager.h"
#include "core/SettingsManager.h"
#include "downloads/DownloadManager.h"
#include "github/GitHubClient.h"
#include "github/RegistryParser.h"
#include "github/ReleaseManager.h"
#include "security/HashVerifier.h"
#include "updater/LauncherUpdateManager.h"
#include "updater/LauncherUpdater.h"
#include "updater/UpdateManifest.h"
#include "updater/UpdateStartup.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using launcher::LauncherUpdateFailure;
using launcher::LauncherUpdateManager;
using launcher::LauncherUpdateState;
using launcher::PackageError;
using launcher::UpdateManifest;
using Kind = launcher::LauncherUpdateFailure::Kind;
using Response = TestHttpServer::Response;
using Request = TestHttpServer::Request;
namespace branding = launcher::branding;
namespace format = launcher::UpdateManifestFormat;

namespace {

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString sha256(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool writeSmallFile(const QString& path, const QByteArray& content)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

/// What the server offers as the newest launcher release.
struct Offer {
    QString version = u"2.0.0"_s;
    QByteArray archive;
    QString assetName;
    bool wrongDigest = false;
    bool noDigest = false;
    QString checksumName; ///< a checksum file next to the asset, "" = none
    QByteArray checksumContent;
};

} // namespace

class LauncherUpdateTests : public QObject {
    Q_OBJECT

private:
    // --- the release and the server -----------------------------------------------------

    /// The release ZIP: the program and the manifest that lists it.
    static QByteArray package(const QString& version, const std::function<void(UpdateManifest&)>& tweak = {},
        const QList<testzip::Entry>& extra = {}, bool withManifest = true,
        const QByteArray& launcherProgram = {})
    {
        const QByteArray launcherExe =
            launcherProgram.isEmpty() ? "launcher program " + version.toUtf8() : launcherProgram;

        UpdateManifest manifest;
        manifest.schemaVersion = 1;
        manifest.product = branding::productName();
        manifest.version = version;
        manifest.launcher = branding::executableFileName();
        manifest.files = {{manifest.launcher, launcherExe.size(), sha256(launcherExe)}};
        if (tweak)
            tweak(manifest);

        QList<testzip::Entry> entries {testzip::file(branding::executableFileName(), launcherExe)};
        if (withManifest)
            entries.append(testzip::file(format::kFileName, format::toJson(manifest), false));
        entries += extra;
        return testzip::build(entries);
    }

    void offer(const QString& version, const QByteArray& archive)
    {
        m_offer = {};
        m_offer.version = version;
        m_offer.archive = archive;
        m_offer.assetName = u"KERJAMI-"_s + version + u"-Windows-x64-Portable.zip"_s;
    }

    QByteArray releaseList() const
    {
        // While the download stalls, the server sends more than the release announced; so the release
        // announces more.
        const qint64 size = m_offer.archive.size() + (m_stall ? 300 * 1024 : 0);
        QJsonObject asset {{u"name"_s, m_offer.assetName}, {u"size"_s, size}, {u"state"_s, u"uploaded"_s}};
        if (!m_offer.noDigest)
            asset.insert(u"digest"_s,
                u"sha256:"_s + (m_offer.wrongDigest ? u"0"_s.repeated(64) : sha256(m_offer.archive)));
        QJsonArray assets {asset};
        if (!m_offer.checksumName.isEmpty())
            assets.append(QJsonObject {{u"name"_s, m_offer.checksumName},
                {u"size"_s, m_offer.checksumContent.size()}, {u"state"_s, u"uploaded"_s}});
        const QJsonObject release {{u"tag_name"_s, u"v"_s + m_offer.version}, {u"name"_s, u"Release"_s},
            {u"body"_s, u"- first change\n- second change"_s}, {u"draft"_s, false}, {u"prerelease"_s, false},
            {u"published_at"_s, u"2026-11-01T12:00:00Z"_s}, {u"assets"_s, assets}};
        return QJsonDocument(QJsonArray {release}).toJson();
    }

    Response handle(const Request& request)
    {
        m_requests.append(request.path);
        Response response;
        if (m_unavailable) {
            response.behavior = Response::Behavior::CloseWithoutAnswer;
            return response;
        }
        const QString repo = u"/"_s + branding::gitHubOwner() + u'/' + branding::launcherRepository();
        if (request.path.startsWith(u"/repos"_s + repo + u"/releases"_s)) {
            response.body = releaseList();
            return response;
        }
        const QString prefix = repo + u"/releases/download/v"_s + m_offer.version + u'/';
        if (request.path == prefix + m_offer.assetName) {
            response.body = m_offer.archive;
            if (m_stall) {
                response.body += QByteArray(300 * 1024, 'x');
                response.behavior = Response::Behavior::SendPartialThenHang;
                response.partialBytes = 20 * 1024;
            }
            return response;
        }
        if (!m_offer.checksumName.isEmpty() && request.path == prefix + m_offer.checksumName) {
            response.body = m_offer.checksumContent;
            return response;
        }
        response.status = 404;
        response.body = "Not Found";
        return response;
    }

    int requestsMatching(const QString& part) const
    {
        int count = 0;
        for (const QString& path : m_requests) {
            if (path.contains(part))
                ++count;
        }
        return count;
    }

    // --- driving the manager ------------------------------------------------------------------

    bool checkAndWait()
    {
        m_manager->checkNow();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 20000) {
            if (m_manager->info().state != LauncherUpdateState::Checking)
                return true;
            QTest::qWait(30);
        }
        return false;
    }

    bool waitForState(LauncherUpdateState state, int timeoutMs = 30000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (m_manager->info().state == state)
                return true;
            QTest::qWait(30);
        }
        return false;
    }

    QString updaterCopy() const
    {
        return m_paths->tempDir() + u"/updater-2.0.0/"_s + branding::executableFileName();
    }
    QString stagingDir() const { return m_paths->tempDir() + u"/update-2.0.0"_s; }

    bool anythingOutsideTheLauncherFolder() const
    {
        // A hostile package must never put a file next to the launcher's folder.
        return QFileInfo::exists(m_dir->filePath(u"evil.txt"_s))
            || QFileInfo::exists(m_dir->filePath(u"evil.dll"_s));
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_paths = std::make_unique<launcher::PathManager>(m_dir->filePath(u"app"_s));
        QVERIFY(QDir().mkpath(m_paths->root()));
        QVERIFY(m_paths->prepare().ok());

        m_requests.clear();
        m_unavailable = false;
        m_stall = false;
        m_busy = false;
        m_starts.clear();
        m_startResult = true;
        offer(u"2.0.0"_s, package(u"2.0.0"_s));

        m_server =
            std::make_unique<TestHttpServer>([this](const Request& request) { return handle(request); });
        QVERIFY(m_server->listen());

        launcher::FetchOptions quick;
        quick.maxAttempts = 1;
        quick.timeoutMs = 3000;
        m_client = std::make_unique<launcher::GitHubClient>(
            launcher::GitHubClient::Policy {{u"http"_s}, {u"127.0.0.1"_s}, true});
        m_settings = std::make_unique<launcher::SettingsManager>(m_paths->settingsFile());
        m_settings->load();
        m_releases = std::make_unique<launcher::ReleaseManager>(
            *m_client, launcher::ReleaseEndpoints {m_server->url(), m_server->url()});
        m_releases->setFetchOptions(quick);
        m_downloads = std::make_unique<launcher::DownloadManager>(
            *m_client, m_paths->tempDir(), m_paths->downloadsDir());
        m_downloads->setFetchOptions(quick);

        m_manager = std::make_unique<LauncherUpdateManager>(
            *m_client, *m_releases, *m_downloads, *m_settings, *m_paths);
        m_manager->setCurrentVersion(u"1.0.0"_s);
        m_manager->setRunningFileName(branding::executableFileName());
        m_manager->setProcessId(4242);
        // The update run is a copy of the running program; here a small stand-in, so that the
        // tests do not copy 30 MB each (the end-to-end test uses the real program).
        QVERIFY(writeSmallFile(m_dir->filePath(u"running-launcher.exe"_s), "the running launcher program"));
        m_manager->setUpdaterSource(m_dir->filePath(u"running-launcher.exe"_s));
        m_manager->setBusyCheck([this] { return m_busy; });
        m_manager->setUpdaterStarter([this](const QString& program, const QString& jobFile) {
            m_starts.append({program, jobFile});
            return m_startResult;
        });
    }

    void cleanup()
    {
        if (m_paths)
            sleepy::writeControlFile(m_paths->root(), u"stop"_s); // no program of a test outlives it
        QTest::qWait(300);
        m_manager.reset();
        m_downloads.reset();
        m_releases.reset();
        m_settings.reset();
        m_client.reset();
        m_server.reset();
        m_paths.reset();
        m_dir.reset();
    }

    // === looking for a newer version ===================================================

    void findsANewerRelease()
    {
        QCOMPARE(m_manager->info().state, LauncherUpdateState::Idle);
        QCOMPARE(m_manager->info().currentVersion, u"1.0.0"_s);
        QVERIFY(checkAndWait());

        const auto info = m_manager->info();
        QCOMPARE(info.state, LauncherUpdateState::Available);
        QCOMPARE(info.latestVersion, u"2.0.0"_s);
        QCOMPARE(info.tag, u"v2.0.0"_s);
        QVERIFY(info.notes.contains(u"second change"_s));
        QCOMPARE(info.assetSize, qint64(m_offer.archive.size()));
        QVERIFY(info.checkedAt.isValid());
        QVERIFY(info.updateKnown());

        // The user is told, and nothing else happens.
        QTest::qWait(300);
        QCOMPARE(requestsMatching(u"/download/"_s), 0);
        QVERIFY(m_starts.isEmpty());
        QVERIFY(!QFileInfo::exists(stagingDir()));
    }

    void sameOrOlderVersionIsUpToDate()
    {
        offer(u"1.0.0"_s, package(u"1.0.0"_s));
        QVERIFY(checkAndWait());
        QCOMPARE(m_manager->info().state, LauncherUpdateState::UpToDate);
        QVERIFY(!m_manager->info().updateKnown());

        offer(u"0.9.0"_s, package(u"0.9.0"_s)); // never a downgrade
        QVERIFY(checkAndWait());
        QCOMPARE(m_manager->info().state, LauncherUpdateState::UpToDate);

        m_manager->install(); // nothing to install
        QTest::qWait(200);
        QCOMPARE(requestsMatching(u"/download/"_s), 0);
    }

    void aPrereleaseIsNotAnUpdate()
    {
        // The server offers only a pre-release tag: no stable release, nothing to announce.
        offer(u"2.0.0-rc.1"_s, package(u"2.0.0-rc.1"_s));
        QVERIFY(checkAndWait());
        QCOMPARE(m_manager->info().state, LauncherUpdateState::Idle);
        QVERIFY(m_manager->info().lastCheckFailed);
    }

    void aFailedCheckKeepsWhatWasKnown()
    {
        m_unavailable = true;
        QVERIFY(checkAndWait());
        QCOMPARE(m_manager->info().state, LauncherUpdateState::Idle); // unknown stays unknown
        QVERIFY(m_manager->info().lastCheckFailed);

        m_unavailable = false;
        QVERIFY(checkAndWait());
        QCOMPARE(m_manager->info().state, LauncherUpdateState::Available);
        QVERIFY(!m_manager->info().lastCheckFailed);

        m_unavailable = true;
        QVERIFY(checkAndWait());
        QCOMPARE(m_manager->info().state,
            LauncherUpdateState::Available); // a dropped connection does not withdraw it
        QCOMPARE(m_manager->info().latestVersion, u"2.0.0"_s);
        QVERIFY(m_manager->info().lastCheckFailed);
    }

    void startChecksOnItsOwnWhenTheSettingIsOn()
    {
        QVERIFY(m_settings->setLauncherUpdateAutoCheck(true));
        m_manager->start();
        QVERIFY(waitForState(LauncherUpdateState::Available, 12000));
    }

    void startDoesNotCheckWhenTheSettingIsOff()
    {
        QVERIFY(m_settings->setLauncherUpdateAutoCheck(false));
        m_manager->start();
        QTest::qWait(4500); // longer than the delay of the first check
        QCOMPARE(requestsMatching(u"/repos/"_s), 0);
        QCOMPARE(m_manager->info().state, LauncherUpdateState::Idle);

        // A check the user asks for is made anyway.
        QVERIFY(checkAndWait());
        QCOMPARE(m_manager->info().state, LauncherUpdateState::Available);
    }

    // === updating ==============================================================================

    void updatesAndHandsOverToTheUpdater()
    {
        QVERIFY(checkAndWait());
        QSignalSpy restart(m_manager.get(), &LauncherUpdateManager::restartRequested);
        QList<LauncherUpdateState> seen;
        connect(m_manager.get(), &LauncherUpdateManager::infoChanged, this, [&] {
            const auto state = m_manager->info().state;
            if (seen.isEmpty() || seen.last() != state)
                seen.append(state);
        });

        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Applying));

        QCOMPARE(restart.size(), 1);
        QVERIFY(seen.contains(LauncherUpdateState::Downloading));
        QVERIFY(seen.contains(LauncherUpdateState::Preparing));
        QVERIFY(seen.contains(LauncherUpdateState::Ready));
        QCOMPARE(seen.last(), LauncherUpdateState::Applying);

        // The update run was started once: a copy of the running program in temp/, with the job next to it.
        QCOMPARE(m_starts.size(), 1);
        QCOMPARE(QDir::cleanPath(m_starts.first().first), QDir::cleanPath(updaterCopy()));
        QCOMPARE(readFile(updaterCopy()), QByteArray("the running launcher program"));
        const auto job = launcher::JsonFile::load(m_starts.first().second);
        QCOMPARE(job.status, launcher::JsonFile::Status::Ok);
        const auto parsed = launcher::UpdaterJob::fromJson(job.object);
        QVERIFY(parsed.has_value());
        QCOMPARE(QDir::cleanPath(parsed->rootDir), QDir::cleanPath(m_paths->root()));
        QCOMPARE(QDir::cleanPath(parsed->stagingDir), QDir::cleanPath(stagingDir()));
        QCOMPARE(parsed->fromVersion, u"1.0.0"_s);
        QCOMPARE(parsed->toVersion, u"2.0.0"_s);
        QCOMPARE(parsed->launcherFile, branding::executableFileName());
        QCOMPARE(parsed->product, branding::productName());
        QCOMPARE(parsed->launcherProcessId, qint64(4242));

        // The release is staged, extracted and checked; the downloaded archive is gone.
        QCOMPARE(readFile(stagingDir() + u"/"_s + branding::executableFileName()),
            QByteArray("launcher program 2.0.0"));
        QVERIFY(QFileInfo::exists(stagingDir() + u"/update-manifest.json"_s));
        QVERIFY(!QFileInfo::exists(m_paths->downloadsDir() + u"/launcher"_s));

        // Nothing of the installed program was touched: replacing it is the updater's job, after the launcher
        // has closed.
        QVERIFY(!QFileInfo::exists(m_paths->root() + u"/"_s + branding::executableFileName()));
    }

    void aChecksumFileCanStandInForTheDigest()
    {
        offer(u"2.0.0"_s, package(u"2.0.0"_s));
        m_offer.noDigest = true;
        m_offer.checksumName = m_offer.assetName + u".sha256"_s;
        m_offer.checksumContent = sha256(m_offer.archive).toUtf8() + "\n";
        QVERIFY(checkAndWait());
        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Applying));
        QCOMPARE(m_starts.size(), 1);
    }

    void withoutAnyHashNothingIsDownloaded()
    {
        m_offer.noDigest = true;
        QVERIFY(checkAndWait());
        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Failed));
        QCOMPARE(m_manager->info().failure.kind, Kind::NoHash);
        QCOMPARE(requestsMatching(u"/download/"_s), 0);
        QVERIFY(m_starts.isEmpty());
    }

    void aWrongHashIsRefusedAndNothingStaysBehind()
    {
        m_offer.wrongDigest = true;
        QVERIFY(checkAndWait());
        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Failed));

        const auto failure = m_manager->info().failure;
        QCOMPARE(failure.kind, Kind::Package);
        QCOMPARE(failure.code, static_cast<int>(PackageError::HashMismatch));
        QVERIFY(m_starts.isEmpty());
        QVERIFY(!QFileInfo::exists(stagingDir()));
        QVERIFY(!QFileInfo::exists(m_paths->downloadsDir() + u"/launcher"_s));

        // The newer version is still known; after the server is fixed, a new look and a second attempt work.
        QVERIFY(m_manager->info().updateKnown());
        m_offer.wrongDigest = false;
        QVERIFY(checkAndWait());
        QCOMPARE(m_manager->info().state, LauncherUpdateState::Available);
        QVERIFY(m_manager->info().failure.kind == Kind::None); // a new look clears the old failure
        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Applying));
    }

    void refusesHostilePackages_data()
    {
        QTest::addColumn<QByteArray>("archive");
        QTest::addColumn<int>("error");

        const auto evil = [](const QString& name) { return testzip::file(name, "evil"); };

        QTest::newRow("zip slip") << package(u"2.0.0"_s, {}, {evil(u"../evil.txt"_s)})
                                  << int(PackageError::ArchiveRejected);
        QTest::newRow("zip slip, backslash")
            << package(u"2.0.0"_s, {}, {evil(u"..\\evil.txt"_s)}) << int(PackageError::ArchiveRejected);
        QTest::newRow("absolute") << package(u"2.0.0"_s, {}, {evil(u"/evil.txt"_s)})
                                  << int(PackageError::ArchiveRejected);
        QTest::newRow("no manifest") << package(u"2.0.0"_s, {}, {}, false)
                                     << int(PackageError::ManifestMissing);
        QTest::newRow("manifest for another product")
            << package(u"2.0.0"_s, [](UpdateManifest& m) { m.product = u"Other"_s; })
            << int(PackageError::ManifestInvalid);
        QTest::newRow("manifest for another version")
            << package(u"2.0.0"_s, [](UpdateManifest& m) { m.version = u"3.0.0"_s; })
            << int(PackageError::VersionMismatch);
        QTest::newRow("manifest lists a file that is not there") << package(
            u"2.0.0"_s, [](UpdateManifest& m) { m.files.append({u"missing.dll"_s, 3, sha256("abc")}); })
                                                                 << int(PackageError::FilesMismatch);
        QTest::newRow("file with another hash")
            << package(u"2.0.0"_s, [](UpdateManifest& m) { m.files[0].sha256 = sha256("something else"); })
            << int(PackageError::FilesMismatch);
        QTest::newRow("file with another size")
            << package(u"2.0.0"_s, [](UpdateManifest& m) { m.files[0].size += 1; })
            << int(PackageError::FilesMismatch);
        QTest::newRow("manifest names the user's settings") << package(u"2.0.0"_s,
            [](UpdateManifest& m) { m.files.append({u"launcher/settings.json"_s, 3, sha256("abc")}); },
            {testzip::file(u"launcher/settings.json"_s, "abc")})
                                                            << int(PackageError::ManifestInvalid);
        QTest::newRow("manifest names a project") << package(u"2.0.0"_s, [](UpdateManifest& m) {
            m.remove.append(u"library/tool/current/Tool.exe"_s);
        }) << int(PackageError::ManifestInvalid);
        QTest::newRow("manifest with a path outside") << package(u"2.0.0"_s, [](UpdateManifest& m) {
            m.files.append({u"../evil.dll"_s, 3, sha256("abc")});
        }) << int(PackageError::ManifestInvalid);
        QTest::newRow("manifest with another launcher name")
            << package(u"2.0.0"_s, [](UpdateManifest& m) { m.launcher = u"Evil.exe"_s; })
            << int(PackageError::ManifestInvalid);
        QTest::newRow("not a zip") << QByteArray("this is not an archive")
                                   << int(PackageError::ArchiveUnreadable);
    }

    void refusesHostilePackages()
    {
        QFETCH(QByteArray, archive);
        QFETCH(int, error);
        offer(u"2.0.0"_s, archive);

        QVERIFY(checkAndWait());
        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Failed));

        const auto failure = m_manager->info().failure;
        QCOMPARE(failure.kind, Kind::Package);
        QCOMPARE(failure.code, error);
        QVERIFY(m_starts.isEmpty());               // the updater was never started
        QVERIFY(!QFileInfo::exists(stagingDir())); // and nothing stays staged
        QVERIFY(!QFileInfo::exists(m_paths->tempDir() + u"/updater-2.0.0"_s));
        QVERIFY(!QFileInfo::exists(m_paths->downloadsDir() + u"/launcher"_s));
        QVERIFY(!anythingOutsideTheLauncherFolder());
    }

    // === the restart ======================================================================

    void waitsForRunningInstallationsBeforeStartingTheUpdate()
    {
        QVERIFY(checkAndWait());
        m_busy = true;
        m_manager->install();
        QCOMPARE(m_manager->info().state, LauncherUpdateState::Available); // not even started
        QCOMPARE(m_manager->info().failure.kind, Kind::Busy);
        QCOMPARE(requestsMatching(u"/download/"_s), 0);

        m_busy = false;
        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Applying));
        QVERIFY(m_manager->info().failure.kind == Kind::None);
    }

    void aReadyUpdateDoesNotRestartWhileProjectsAreBeingInstalled()
    {
        QVERIFY(checkAndWait());
        // An installation begins while the update is being prepared.
        connect(m_manager.get(), &LauncherUpdateManager::infoChanged, this, [this] {
            if (m_manager->info().state == LauncherUpdateState::Preparing)
                m_busy = true;
        });
        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Ready));
        QTest::qWait(200);
        QCOMPARE(m_manager->info().state, LauncherUpdateState::Ready); // waits
        QCOMPARE(m_manager->info().failure.kind, Kind::Busy);
        QVERIFY(m_starts.isEmpty());

        m_manager->apply(); // still busy: still refused
        QCOMPARE(m_manager->info().state, LauncherUpdateState::Ready);
        QVERIFY(m_starts.isEmpty());

        m_busy = false;
        m_manager->apply();
        QCOMPARE(m_manager->info().state, LauncherUpdateState::Applying);
        QCOMPARE(m_starts.size(), 1);
    }

    void aLauncherUnderAnotherNameDoesNotUpdateItself()
    {
        m_manager->setRunningFileName(u"Renamed.exe"_s);
        QVERIFY(checkAndWait());
        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Failed));
        QCOMPARE(m_manager->info().failure.kind, Kind::Handoff);
        QCOMPARE(m_manager->info().failure.code, 1);
        QVERIFY(m_starts.isEmpty());
        QVERIFY(!QFileInfo::exists(stagingDir())); // nothing stays staged
    }

    void anUpdaterThatCannotBeStartedIsReported()
    {
        m_startResult = false;
        QSignalSpy restart(m_manager.get(), &LauncherUpdateManager::restartRequested);
        QVERIFY(checkAndWait());
        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Failed));
        QCOMPARE(m_manager->info().failure.kind, Kind::Handoff);
        QCOMPARE(m_manager->info().failure.code, 3);
        QCOMPARE(restart.size(), 0); // the launcher keeps running
        QVERIFY(!QFileInfo::exists(stagingDir()));
    }

    // === cancelling ================================================================================

    void cancelDuringTheDownload()
    {
        m_stall = true;
        m_offer.archive = package(u"2.0.0"_s);
        QVERIFY(checkAndWait());
        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Downloading));
        QTRY_VERIFY_WITH_TIMEOUT(m_manager->info().done > 0, 10000);

        m_manager->cancel();
        QCOMPARE(m_manager->info().state, LauncherUpdateState::Available);
        QVERIFY(m_manager->info().failure.kind == Kind::None);
        QVERIFY(QDir(m_paths->tempDir()).isEmpty());
        QVERIFY(m_starts.isEmpty());
    }

    void cancelDuringThePreparation()
    {
        // A large archive, so that verifying and extracting takes a moment.
        offer(u"2.0.0"_s,
            package(u"2.0.0"_s, {}, {testzip::file(u"data/big.bin"_s, QByteArray(60'000'000, 'x'), false)}));
        QVERIFY(checkAndWait());
        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Preparing));

        m_manager->cancel();
        QVERIFY(waitForState(LauncherUpdateState::Available)
            || m_manager->info().state == LauncherUpdateState::Applying);
        if (m_manager->info().state == LauncherUpdateState::Available) {
            QVERIFY(m_manager->info().failure.kind == Kind::None);
            QVERIFY(!QFileInfo::exists(stagingDir()));
            QVERIFY(m_starts.isEmpty());
        }
    }

    void cancelOfAReadyUpdateDiscardsIt()
    {
        QVERIFY(checkAndWait());
        connect(m_manager.get(), &LauncherUpdateManager::infoChanged, this, [this] {
            if (m_manager->info().state == LauncherUpdateState::Preparing)
                m_busy = true;
        });
        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Ready));
        QVERIFY(QFileInfo::exists(stagingDir()));

        m_manager->cancel();
        QCOMPARE(m_manager->info().state, LauncherUpdateState::Available);
        QVERIFY(!QFileInfo::exists(stagingDir()));
        QVERIFY(m_starts.isEmpty());
    }

    // === the whole chain, with the real updater program =========================================

    void selfUpdateEndToEnd()
    {
        // The manager prepares a real package and starts the real program in update mode,
        // which waits for the old launcher, replaces the files and starts the new launcher.
        const QString root = m_paths->root();
        const QString programPath = QDir::cleanPath(
            QCoreApplication::applicationDirPath() + u"/../bin/"_s + branding::executableFileName());
        QVERIFY2(QFileInfo::exists(programPath), qPrintable(programPath));
        const QByteArray oldLauncher = readFile(sleepy::appPath());
        const QByteArray newLauncher = oldLauncher + "NEW-2.0.0";
        QVERIFY(!oldLauncher.isEmpty());

        const auto write = [](const QString& path, const QByteArray& content) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
        };
        QVERIFY(write(root + u"/"_s + branding::executableFileName(), oldLauncher));
        QVERIFY(write(root + u"/library/tool/current/Tool.exe"_s, "a project"));
        QVERIFY(write(root + u"/downloads/other/keep.zip"_s, "a download"));
        const QByteArray settingsBefore = readFile(m_paths->settingsFile());
        QVERIFY(!settingsBefore.isEmpty());

        QProcess oldProcess;
        oldProcess.start(root + u"/"_s + branding::executableFileName(), {});
        QVERIFY(oldProcess.waitForStarted(10000));
        m_manager->setProcessId(oldProcess.processId());
        m_manager->setUpdaterSource(programPath);
        m_manager->setUpdaterStarter([](const QString& program, const QString& jobFile) {
            return QProcess::startDetached(program,
                {QString::fromLatin1(launcher::LauncherUpdater::modeFlag()), u"--job"_s, jobFile,
                    u"--quiet"_s},
                QFileInfo(program).absolutePath());
        });

        offer(u"2.0.0"_s, package(u"2.0.0"_s, {}, {}, true, newLauncher));
        QVERIFY(checkAndWait());
        m_manager->install();
        QVERIFY(waitForState(LauncherUpdateState::Applying, 60000));

        // The update run is going, but the old launcher is still there: nothing has changed.
        QTest::qWait(1500);
        QCOMPARE(readFile(root + u"/"_s + branding::executableFileName()), oldLauncher);
        QVERIFY(!QFileInfo::exists(m_paths->updateReportFile()));

        // The launcher closes, as it does after restartRequested().
        QVERIFY(write(root + u"/stop"_s, "1"));
        oldProcess.waitForFinished(10000);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(m_paths->updateReportFile()), 60000);

        QCOMPARE(readFile(root + u"/"_s + branding::executableFileName()), newLauncher);
        QCOMPARE(readFile(m_paths->settingsFile()), settingsBefore);
        QCOMPARE(readFile(root + u"/library/tool/current/Tool.exe"_s), QByteArray("a project"));
        QCOMPARE(readFile(root + u"/downloads/other/keep.zip"_s), QByteArray("a download"));
        QVERIFY(!QFileInfo::exists(stagingDir()));

        // The next start of the launcher learns how it went, once.
        const launcher::StartupUpdateOutcome startup = launcher::UpdateStartup::run(*m_paths, 10000);
        QVERIFY(!startup.updaterStillRunning);
        QVERIFY(startup.report.has_value());
        QCOMPARE(startup.report->status, launcher::UpdateReport::Status::Success);
        QCOMPARE(startup.report->fromVersion, u"1.0.0"_s);
        QCOMPARE(startup.report->toVersion, u"2.0.0"_s);
        QVERIFY(startup.report->launcherStarted);
        QVERIFY(!launcher::UpdateStartup::run(*m_paths, 1000).report.has_value());

        // And the new launcher really was started.
        QStringList lines;
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                lines =
                    QString::fromUtf8(readFile(root + u"/started.log"_s)).split(u'\n', Qt::SkipEmptyParts);
                for (QString& line : lines)
                    line = line.trimmed();
                return lines.size() >= 2;
            }(),
            10000);
        QCOMPARE(lines.last(), QString::number(newLauncher.size()));
    }

    // === registry ====================================================================================

    void theRegistryCannotUseTheLauncherId()
    {
        QVERIFY(launcher::RegistryParser::isValidProjectId(u"launcher"_s));
        QVERIFY(launcher::RegistryParser::isReservedProjectId(u"launcher"_s));
        QVERIFY(!launcher::RegistryParser::isReservedProjectId(u"launcher-tools"_s));
    }

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<launcher::PathManager> m_paths;
    std::unique_ptr<TestHttpServer> m_server;
    std::unique_ptr<launcher::GitHubClient> m_client;
    std::unique_ptr<launcher::SettingsManager> m_settings;
    std::unique_ptr<launcher::ReleaseManager> m_releases;
    std::unique_ptr<launcher::DownloadManager> m_downloads;
    std::unique_ptr<LauncherUpdateManager> m_manager;

    Offer m_offer;
    QStringList m_requests;
    bool m_unavailable = false;
    bool m_stall = false;
    bool m_busy = false;
    bool m_startResult = true;
    QList<QPair<QString, QString>> m_starts;
};

QTEST_GUILESS_MAIN(LauncherUpdateTests)
#include "LauncherUpdateTests.moc"
