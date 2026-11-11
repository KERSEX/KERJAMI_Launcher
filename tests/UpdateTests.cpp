// Project updates: finding new versions of installed projects, manual and
// automatic mode, a running project that makes an update wait, failures, and
// the queue. A server on the local machine plays GitHub; the "projects" are
// copies of a real program (SleepyApp) so that "running" is real too.

#include "SleepyProject.h"
#include "TestHttpServer.h"
#include "ZipBuilder.h"
#include "core/PathManager.h"
#include "core/SettingsManager.h"
#include "downloads/DownloadManager.h"
#include "github/GitHubClient.h"
#include "github/RegistryManager.h"
#include "github/ReleaseManager.h"
#include "installer/InstallService.h"
#include "installer/InstalledProjects.h"
#include "installer/Installer.h"
#include "installer/LibraryLayout.h"
#include "installer/Uninstaller.h"
#include "process/ProcessManager.h"
#include "updater/ProjectUpdateManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <windows.h>

using namespace Qt::StringLiterals;
using launcher::UpdateMode;
using launcher::UpdateState;
using Response = TestHttpServer::Response;
using Request = TestHttpServer::Request;

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

#ifdef Q_OS_WIN
/// Keeps a file open the way a program does: nobody may delete or rename it.
class FileLock {
public:
    explicit FileLock(const QString& path)
        : m_handle(CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()),
              GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
    {
    }
    ~FileLock()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
    }
    bool isLocked() const { return m_handle != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_handle;
};
#endif

/// What the "server" offers for one repository.
struct Offer {
    QString version; ///< Latest stable version, empty = no release.
    QString notes = u"Changes of this version"_s;
    bool wrongDigest = false;
    bool noDigest = false; ///< GitHub gives no digest and the release has no checksum file.
    QByteArray archive;
    QString assetName;
};

} // namespace

class UpdateTests : public QObject {
    Q_OBJECT

private:
    // --- the world: server, registry, managers --------------------------------

    QByteArray releaseList(const QString& repo) const
    {
        const Offer offer = m_offers.value(repo);
        if (offer.version.isEmpty())
            return "[]";
        const QString digest = offer.wrongDigest ? u"0"_s.repeated(64) : sha256(offer.archive);
        QJsonObject asset {
            {u"name"_s, offer.assetName}, {u"size"_s, offer.archive.size()}, {u"state"_s, u"uploaded"_s}};
        if (!offer.noDigest)
            asset.insert(u"digest"_s, u"sha256:"_s + digest);
        const QJsonObject release {{u"tag_name"_s, u"v"_s + offer.version}, {u"name"_s, u"Release"_s},
            {u"body"_s, offer.notes}, {u"draft"_s, false}, {u"prerelease"_s, false},
            {u"published_at"_s, u"2026-11-01T12:00:00Z"_s}, {u"assets"_s, QJsonArray {asset}}};
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
        for (auto it = m_offers.cbegin(); it != m_offers.cend(); ++it) {
            const QString repo = it.key();
            if (request.path.startsWith(u"/repos/KERSEX/"_s + repo + u"/releases"_s)) {
                response.body = releaseList(repo);
                return response;
            }
            if (request.path
                == u"/KERSEX/"_s + repo + u"/releases/download/v"_s + it->version + u'/' + it->assetName) {
                response.body = it->archive;
                return response;
            }
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

    /// An archive for a version: the program and a file that says which version it is.
    static QByteArray archiveFor(const QString& version)
    {
        return testzip::build({testzip::file(u"Tool.exe"_s, readFile(sleepy::appPath()), false),
            testzip::file(u"version.txt"_s, version.toUtf8(), false)});
    }

    void offer(const QString& repo, const QString& version)
    {
        Offer offer;
        offer.version = version;
        offer.archive = archiveFor(version);
        offer.assetName = repo + u"-"_s + version + u".zip"_s;
        m_offers.insert(repo, offer);
    }

    static QJsonObject projectEntry(const QString& id, const QString& repo)
    {
        return {{u"id"_s, id}, {u"name"_s, id.toUpper()}, {u"description"_s, u"Test project"_s},
            {u"owner"_s, u"KERSEX"_s}, {u"repository"_s, repo}, {u"category"_s, u"Tools"_s},
            {u"tags"_s, QJsonArray {}}, {u"enabled"_s, true}, {u"featured"_s, false},
            {u"release_channel"_s, u"stable"_s}, {u"asset_pattern"_s, repo + u"-*.zip"_s},
            {u"executable"_s, u"Tool.exe"_s}, {u"install_directory"_s, repo}};
    }

    bool writeRegistry(const QJsonArray& projects)
    {
        const QJsonObject registry {
            {u"schema_version"_s, 1}, {u"registry_name"_s, u"Test"_s}, {u"projects"_s, projects}};
        return m_registry->load(QJsonDocument(registry).toJson());
    }

    /// A project, installed at the given version (a real program, started from its folder).
    bool installProject(const QString& id, const QString& repo, const QString& version)
    {
        if (!sleepy::install(*m_layout, *m_installed, id, repo, version))
            return false;
        // The file that tells the version, as the archive of that version has it.
        return sleepy::writeControlFile(m_layout->currentDir(repo), u"version.txt"_s, version.toUtf8());
    }

    QString currentDir(const QString& repo) const { return m_layout->currentDir(repo); }
    QString installedVersion(const QString& id) const { return m_installed->get(id)->version; }

    /// Waits for a round of checks to end.
    bool checkAndWait()
    {
        QSignalSpy finished(m_updates.get(), &launcher::ProjectUpdateManager::checkFinished);
        m_updates->checkAll();
        return finished.size() > 0 || finished.wait(20000);
    }

    bool waitForState(const QString& id, UpdateState state, int timeoutMs = 30000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (m_updates->info(id).state == state)
                return true;
            QTest::qWait(50);
        }
        return false;
    }

    /// Looks up the running projects until the state is reached.
    bool waitForStateWithLookups(const QString& id, UpdateState state, int timeoutMs = 30000)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            m_processes->refresh();
            if (m_updates->info(id).state == state)
                return true;
            QTest::qWait(60);
        }
        return false;
    }

private slots:
    void initTestCase() { QVERIFY2(QFileInfo::exists(sleepy::appPath()), "SleepyApp.exe is not built"); }

    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        m_paths = std::make_unique<launcher::PathManager>(m_root->path());
        QVERIFY(m_paths->prepare().ok());

        m_offers.clear();
        m_requests.clear();
        m_unavailable = false;
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
        m_registry = std::make_unique<launcher::RegistryManager>();
        m_layout = std::make_unique<launcher::LibraryLayout>(m_paths->libraryDir(), m_paths->tempDir());
        m_installed = std::make_unique<launcher::InstalledProjects>(m_paths->stateFile());
        m_installed->load();
        m_releases = std::make_unique<launcher::ReleaseManager>(
            *m_client, launcher::ReleaseEndpoints {m_server->url(), m_server->url()});
        m_releases->setFetchOptions(quick);
        m_downloads = std::make_unique<launcher::DownloadManager>(
            *m_client, m_paths->tempDir(), m_paths->downloadsDir());
        m_downloads->setFetchOptions(quick);
        m_installer = std::make_unique<launcher::Installer>(*m_layout, *m_installed);
        m_uninstaller = std::make_unique<launcher::Uninstaller>(*m_layout, *m_installed);
        m_service = std::make_unique<launcher::InstallService>(
            *m_client, *m_releases, *m_downloads, *m_installer, *m_uninstaller, *m_installed, *m_layout);
        m_processes = std::make_unique<launcher::ProcessManager>(*m_layout, *m_installed);
        m_processes->setPolling(false); // the tests decide when to look
        m_installer->setRunningCheck([this](const QString& id) { return m_processes->isRunningNow(id); });
        m_uninstaller->setRunningCheck([this](const QString& id) { return m_processes->isRunningNow(id); });
        m_service->setStarter([this](const QString& id) { return m_processes->start(id); });
        m_updates = std::make_unique<launcher::ProjectUpdateManager>(
            *m_client, *m_registry, *m_releases, *m_service, *m_processes, *m_installed, *m_settings);
        m_updates->setMinCheckSpacingSeconds(0);
    }

    void cleanup()
    {
        // No program of a test may outlive it.
        for (const launcher::InstalledProject& project : m_installed->all())
            sleepy::stop(m_layout->currentDir(project.installDirectory));
        QTest::qWait(300);
        m_updates.reset();
        m_processes.reset();
        m_service.reset();
        m_uninstaller.reset();
        m_installer.reset();
        m_downloads.reset();
        m_releases.reset();
        m_installed.reset();
        m_layout.reset();
        m_registry.reset();
        m_settings.reset();
        m_client.reset();
        m_server.reset();
        m_paths.reset();
        m_root.reset();
    }

    // === finding updates ======================================================

    void findsANewerVersionAndWaitsInManualMode()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));

        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::Unknown);
        QVERIFY(checkAndWait());

        const launcher::UpdateInfo info = m_updates->info(u"tool"_s);
        QCOMPARE(info.state, UpdateState::Available);
        QCOMPARE(info.installedVersion, u"1.0.0"_s);
        QCOMPARE(info.latestVersion, u"1.1.0"_s);
        QCOMPARE(info.tag, u"v1.1.0"_s);
        QCOMPARE(info.notes, u"Changes of this version"_s);
        QCOMPARE(info.assetSize, qint64(m_offers.value(u"Tool"_s).archive.size()));
        QVERIFY(info.checkedAt.isValid());
        QVERIFY(info.updateKnown());

        // Manual mode announces and does nothing else: no download, nothing replaced.
        QTest::qWait(300);
        QCOMPARE(requestsMatching(u"/download/"_s), 0);
        QCOMPARE(installedVersion(u"tool"_s), u"1.0.0"_s);
        QCOMPARE(readFile(currentDir(u"Tool"_s) + u"/version.txt"_s), QByteArray("1.0.0"));
        QVERIFY(m_updates->active().isEmpty());
    }

    void sameOrOlderVersionIsUpToDate()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"2.0.0"_s));
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));

        offer(u"Tool"_s, u"2.0.0"_s);
        QVERIFY(checkAndWait());
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::UpToDate);

        // A release that is older than the installed version is never an update, however it came to be.
        offer(u"Tool"_s, u"1.5.0"_s);
        QVERIFY(checkAndWait());
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::UpToDate);
        QCOMPARE(m_updates->info(u"tool"_s).latestVersion, u"1.5.0"_s);
        QVERIFY(!m_updates->info(u"tool"_s).updateKnown());
    }

    void onlyInstalledProjectsAreAskedAbout()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        offer(u"Other"_s, u"3.0.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s), projectEntry(u"other"_s, u"Other"_s)}));

        // Automatic mode for a project that is not installed changes nothing: a project is never
        // installed because it is in the registry.
        QVERIFY(m_settings->setUpdateMode(u"other"_s, UpdateMode::Automatic));
        QVERIFY(checkAndWait());
        QTest::qWait(300);

        QCOMPARE(requestsMatching(u"/repos/KERSEX/Other"_s), 0);
        QCOMPARE(requestsMatching(u"Other"_s), 0);
        QVERIFY(!m_installed->contains(u"other"_s));
        QVERIFY(!QFileInfo::exists(m_layout->projectDir(u"Other"_s)));
        QCOMPARE(m_updates->info(u"other"_s).state, UpdateState::Unknown);
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::Available);
    }

    void aProjectOutsideTheRegistryIsLeftAlone()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({})); // no longer approved
        QVERIFY(checkAndWait());
        QCOMPARE(requestsMatching(u"/repos/"_s), 0);
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::Unknown);
    }

    void aNetworkProblemNeitherCreatesNorRemovesAnUpdate()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));

        // Nothing known yet: a failed check stays "unknown", it is no "up to date".
        m_unavailable = true;
        QVERIFY(checkAndWait());
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::Unknown);
        QVERIFY(m_updates->info(u"tool"_s).lastCheckFailed);

        // A known update stays known.
        m_unavailable = false;
        QVERIFY(checkAndWait());
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::Available);
        QVERIFY(!m_updates->info(u"tool"_s).lastCheckFailed);

        m_unavailable = true;
        QVERIFY(checkAndWait());
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::Available);
        QCOMPARE(m_updates->info(u"tool"_s).latestVersion, u"1.1.0"_s);
        QVERIFY(m_updates->info(u"tool"_s).lastCheckFailed);

        // And nothing was installed while the connection was down.
        QCOMPARE(installedVersion(u"tool"_s), u"1.0.0"_s);
    }

    void checksAreSpacedOut()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.0.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));
        m_updates->setMinCheckSpacingSeconds(60);

        QVERIFY(checkAndWait());
        QVERIFY(checkAndWait());
        QVERIFY(checkAndWait());
        QCOMPARE(requestsMatching(u"/repos/"_s), 1); // asked once, not three times
    }

    // === manual updates =======================================================

    void manualUpdateInstallsAfterTheUserAsks()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));
        QVERIFY(checkAndWait());

        QList<UpdateState> seen;
        connect(
            m_updates.get(), &launcher::ProjectUpdateManager::updateChanged, this, [&](const QString& id) {
                const UpdateState state = m_updates->info(id).state;
                if (seen.isEmpty() || seen.last() != state)
                    seen.append(state);
            });

        m_updates->applyUpdate(u"tool"_s);
        QVERIFY(waitForState(u"tool"_s, UpdateState::UpToDate));

        QCOMPARE(installedVersion(u"tool"_s), u"1.1.0"_s);
        QCOMPARE(readFile(currentDir(u"Tool"_s) + u"/version.txt"_s), QByteArray("1.1.0"));
        QCOMPARE(readFile(m_layout->backupsDir(u"Tool"_s) + u"/1.0.0/version.txt"_s), QByteArray("1.0.0"));
        QVERIFY(seen.contains(UpdateState::Downloading));
        QVERIFY(seen.contains(UpdateState::Installing));
        QCOMPARE(seen.last(), UpdateState::UpToDate);
        QVERIFY(m_updates->info(u"tool"_s).failure.kind == launcher::OperationFailure::Kind::None);
        QVERIFY(QDir(m_paths->tempDir()).isEmpty());
        QVERIFY(!QFileInfo::exists(m_paths->downloadsDir() + u"/tool"_s + u"/Tool-1.1.0.zip"_s));
    }

    void applyUpdateDoesNothingWithoutAnAnnouncedUpdate()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.0.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));

        m_updates->applyUpdate(u"tool"_s); // never checked
        QVERIFY(checkAndWait());
        m_updates->applyUpdate(u"tool"_s); // up to date
        QTest::qWait(300);
        QCOMPARE(requestsMatching(u"/download/"_s), 0);
        QVERIFY(m_updates->active().isEmpty());
    }

    // === automatic updates =====================================================

    void automaticModeInstallsWithoutAsking()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));
        QVERIFY(m_settings->setUpdateMode(u"tool"_s, UpdateMode::Automatic));

        QVERIFY(checkAndWait());
        QVERIFY(waitForState(u"tool"_s, UpdateState::UpToDate));
        QCOMPARE(installedVersion(u"tool"_s), u"1.1.0"_s);
        QCOMPARE(readFile(currentDir(u"Tool"_s) + u"/version.txt"_s), QByteArray("1.1.0"));
    }

    void aRunningProjectIsNeverTouched()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));
        QVERIFY(m_settings->setUpdateMode(u"tool"_s, UpdateMode::Automatic));

        QVERIFY(m_processes->start(u"tool"_s).ok());
        QVERIFY(checkAndWait());
        QVERIFY(waitForState(u"tool"_s, UpdateState::Pending)); // "Update Pending"

        // Downloaded and verified, but nothing replaced while the program runs.
        QCOMPARE(installedVersion(u"tool"_s), u"1.0.0"_s);
        QCOMPARE(readFile(currentDir(u"Tool"_s) + u"/version.txt"_s), QByteArray("1.0.0"));
        QVERIFY(!QFileInfo::exists(m_layout->backupsDir(u"Tool"_s)));
        QCOMPARE(m_updates->active(), QStringList {u"tool"_s});
        QCOMPARE(m_service->status(u"tool"_s).stage, launcher::ProjectStatus::Stage::Prepared);

        // Time passes and the project still runs.
        for (int i = 0; i < 5; ++i) {
            m_processes->refresh();
            QTest::qWait(100);
        }
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::Pending);
        QCOMPARE(installedVersion(u"tool"_s), u"1.0.0"_s);

        // The program ends: the update is installed.
        sleepy::stop(currentDir(u"Tool"_s));
        QVERIFY(waitForStateWithLookups(u"tool"_s, UpdateState::UpToDate));
        QCOMPARE(installedVersion(u"tool"_s), u"1.1.0"_s);
        QCOMPARE(readFile(currentDir(u"Tool"_s) + u"/version.txt"_s), QByteArray("1.1.0"));
        QCOMPARE(readFile(m_layout->backupsDir(u"Tool"_s) + u"/1.0.0/version.txt"_s), QByteArray("1.0.0"));
        QVERIFY(m_updates->active().isEmpty());
    }

    void aRunningProjectAlsoMakesAManualUpdateWait()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));
        QVERIFY(checkAndWait());
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::Available);

        QVERIFY(m_processes->start(u"tool"_s).ok());
        m_updates->applyUpdate(u"tool"_s);
        QVERIFY(waitForState(u"tool"_s, UpdateState::Pending));
        QCOMPARE(installedVersion(u"tool"_s), u"1.0.0"_s);

        sleepy::stop(currentDir(u"Tool"_s));
        QVERIFY(waitForStateWithLookups(u"tool"_s, UpdateState::UpToDate));
        QCOMPARE(installedVersion(u"tool"_s), u"1.1.0"_s);
    }

    void aLockedFileFailsTheUpdateWithoutDamageAndItIsTriedAgainLater()
    {
        // The last safety net: something holds a file of the project open that the
        // process look-up does not see (not a program of the project). Windows refuses to
        // replace the folder, the update fails with a clear reason and nothing changes.
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));
        QVERIFY(m_settings->setUpdateMode(u"tool"_s, UpdateMode::Automatic));

        {
            FileLock lock(currentDir(u"Tool"_s) + u"/version.txt"_s);
            QVERIFY(lock.isLocked());
            QVERIFY(!m_processes->isRunningNow(u"tool"_s));

            QVERIFY(checkAndWait());
            QVERIFY(waitForState(u"tool"_s, UpdateState::Failed));
            const launcher::UpdateInfo info = m_updates->info(u"tool"_s);
            QCOMPARE(info.failure.kind, launcher::OperationFailure::Kind::Install);
            QCOMPARE(info.failure.code, static_cast<int>(launcher::InstallError::ProjectRunning));
            QCOMPARE(installedVersion(u"tool"_s), u"1.0.0"_s);
            QCOMPARE(readFile(currentDir(u"Tool"_s) + u"/version.txt"_s), QByteArray("1.0.0"));
            QVERIFY(!QFileInfo::exists(m_layout->backupsDir(u"Tool"_s) + u"/1.0.0"_s));
        }

        // "In use" is not the release's fault: the next round tries again, and this time it works.
        QVERIFY(checkAndWait());
        QVERIFY(waitForState(u"tool"_s, UpdateState::UpToDate));
        QCOMPARE(installedVersion(u"tool"_s), u"1.1.0"_s);
    }

    void cancelWithdrawsAPendingUpdate()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));
        QVERIFY(m_settings->setUpdateMode(u"tool"_s, UpdateMode::Automatic));
        QVERIFY(m_processes->start(u"tool"_s).ok());
        QVERIFY(checkAndWait());
        QVERIFY(waitForState(u"tool"_s, UpdateState::Pending));

        m_updates->cancel(u"tool"_s);
        QVERIFY(waitForState(u"tool"_s, UpdateState::Available));
        QVERIFY(m_updates->active().isEmpty());
        QVERIFY(QDir(m_paths->tempDir()).isEmpty());
        QVERIFY(!QFileInfo::exists(m_paths->downloadsDir() + u"/tool"_s)); // the prepared archive is gone

        // Automatic mode does not start the cancelled version again by itself ...
        sleepy::stop(currentDir(u"Tool"_s));
        QVERIFY(checkAndWait());
        QTest::qWait(300);
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::Available);
        QCOMPARE(requestsMatching(u"/download/"_s), 1);

        // ... but the user can still ask for it.
        QTest::qWait(300);
        m_processes->refresh();
        m_updates->applyUpdate(u"tool"_s);
        QVERIFY(waitForStateWithLookups(u"tool"_s, UpdateState::UpToDate));
        QCOMPARE(installedVersion(u"tool"_s), u"1.1.0"_s);
    }

    void switchingToManualWithdrawsAnAutomaticUpdate()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));
        QVERIFY(m_settings->setUpdateMode(u"tool"_s, UpdateMode::Automatic));
        QVERIFY(m_processes->start(u"tool"_s).ok());
        QVERIFY(checkAndWait());
        QVERIFY(waitForState(u"tool"_s, UpdateState::Pending));

        QVERIFY(m_settings->setUpdateMode(u"tool"_s, UpdateMode::Manual));
        QVERIFY(waitForState(u"tool"_s, UpdateState::Available));
        sleepy::stop(currentDir(u"Tool"_s));
        QVERIFY(refreshAndWait(500));
        QCOMPARE(installedVersion(u"tool"_s), u"1.0.0"_s); // nothing happened behind the user's back
    }

    void switchingToAutomaticStartsAKnownUpdate()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));
        QVERIFY(checkAndWait());
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::Available);

        QVERIFY(m_settings->setUpdateMode(u"tool"_s, UpdateMode::Automatic));
        QVERIFY(waitForState(u"tool"_s, UpdateState::UpToDate));
        QCOMPARE(installedVersion(u"tool"_s), u"1.1.0"_s);
    }

    // === failures ==============================================================

    void aFailedUpdateLeavesTheProjectAsItWasAndIsNotRetriedAutomatically()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        m_offers[u"Tool"_s].wrongDigest = true; // the release's hash does not fit its file
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));
        QVERIFY(m_settings->setUpdateMode(u"tool"_s, UpdateMode::Automatic));

        QVERIFY(checkAndWait());
        QVERIFY(waitForState(u"tool"_s, UpdateState::Failed));

        const launcher::UpdateInfo info = m_updates->info(u"tool"_s);
        QCOMPARE(info.failure.kind, launcher::OperationFailure::Kind::Install);
        QCOMPARE(info.failure.code, static_cast<int>(launcher::InstallError::HashMismatch));
        QCOMPARE(installedVersion(u"tool"_s), u"1.0.0"_s);
        QCOMPARE(readFile(currentDir(u"Tool"_s) + u"/version.txt"_s), QByteArray("1.0.0"));
        QVERIFY(!QFileInfo::exists(m_layout->backupsDir(u"Tool"_s)));
        QVERIFY(QDir(m_paths->tempDir()).isEmpty());
        QVERIFY(!QFileInfo::exists(m_paths->downloadsDir() + u"/tool"_s)); // the bad file was removed

        // The next rounds do not download the same broken version again.
        QCOMPARE(requestsMatching(u"/download/"_s), 1);
        QVERIFY(checkAndWait());
        QVERIFY(checkAndWait());
        QTest::qWait(300);
        QCOMPARE(requestsMatching(u"/download/"_s), 1);
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::Failed);

        // The user may try again; it fails again for the same reason.
        m_updates->applyUpdate(u"tool"_s);
        QVERIFY(waitForState(u"tool"_s, UpdateState::Failed));
        QCOMPARE(requestsMatching(u"/download/"_s), 2);

        // A newer, correct version starts over.
        offer(u"Tool"_s, u"1.2.0"_s);
        QVERIFY(checkAndWait());
        QVERIFY(waitForState(u"tool"_s, UpdateState::UpToDate));
        QCOMPARE(installedVersion(u"tool"_s), u"1.2.0"_s);
        QVERIFY(m_updates->info(u"tool"_s).failure.kind == launcher::OperationFailure::Kind::None);
    }

    void aReleaseWithoutAnyHashIsNotDownloaded()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        m_offers[u"Tool"_s].noDigest = true;
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));
        QVERIFY(m_settings->setUpdateMode(u"tool"_s, UpdateMode::Automatic));

        QVERIFY(checkAndWait());
        QVERIFY(waitForState(u"tool"_s, UpdateState::Failed));
        QCOMPARE(m_updates->info(u"tool"_s).failure.kind, launcher::OperationFailure::Kind::NoHash);
        QCOMPARE(requestsMatching(u"/download/"_s), 0); // nothing was downloaded
        QCOMPARE(installedVersion(u"tool"_s), u"1.0.0"_s);
    }

    // === several projects =======================================================

    void eachProjectHasItsOwnMode()
    {
        QVERIFY(installProject(u"alpha"_s, u"Alpha"_s, u"1.0.0"_s));
        QVERIFY(installProject(u"beta"_s, u"Beta"_s, u"1.0.0"_s));
        offer(u"Alpha"_s, u"1.1.0"_s);
        offer(u"Beta"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"alpha"_s, u"Alpha"_s), projectEntry(u"beta"_s, u"Beta"_s)}));
        QVERIFY(m_settings->setUpdateMode(u"alpha"_s, UpdateMode::Automatic));

        QVERIFY(checkAndWait());
        QVERIFY(waitForState(u"alpha"_s, UpdateState::UpToDate));
        QTest::qWait(300);

        QCOMPARE(installedVersion(u"alpha"_s), u"1.1.0"_s);
        QCOMPARE(installedVersion(u"beta"_s), u"1.0.0"_s);
        QCOMPARE(m_updates->info(u"beta"_s).state, UpdateState::Available);
        QCOMPARE(m_settings->updateMode(u"alpha"_s), UpdateMode::Automatic);
        QCOMPARE(m_settings->updateMode(u"beta"_s), UpdateMode::Manual);
    }

    void theQueueLimitsParallelUpdates()
    {
        const QStringList ids {u"p1"_s, u"p2"_s, u"p3"_s, u"p4"_s};
        QJsonArray registry;
        for (const QString& id : ids) {
            const QString repo = id.toUpper();
            QVERIFY(installProject(id, repo, u"1.0.0"_s));
            offer(repo, u"1.1.0"_s);
            registry.append(projectEntry(id, repo));
            QVERIFY(m_settings->setUpdateMode(id, UpdateMode::Automatic));
        }
        QVERIFY(writeRegistry(registry));

        int mostActive = 0;
        bool queuedSeen = false;
        connect(m_updates.get(), &launcher::ProjectUpdateManager::updateChanged, this, [&] {
            mostActive = qMax(mostActive, int(m_updates->active().size()));
            queuedSeen = queuedSeen || !m_updates->queue().isEmpty();
        });

        QVERIFY(checkAndWait());
        QElapsedTimer timer;
        timer.start();
        bool allDone = false;
        while (timer.elapsed() < 40000 && !allDone) {
            QTest::qWait(50);
            allDone = true;
            for (const QString& id : ids)
                allDone = allDone && m_updates->info(id).state == UpdateState::UpToDate;
        }
        QVERIFY(allDone);
        QVERIFY(mostActive <= launcher::ProjectUpdateManager::kMaxParallelUpdates);
        QVERIFY2(queuedSeen, "four updates and two places: somebody had to wait");
        for (const QString& id : ids)
            QCOMPARE(installedVersion(id), u"1.1.0"_s);
    }

    // === uninstalling =============================================================

    void uninstallingForgetsTheProject()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));
        QVERIFY(m_settings->setUpdateMode(u"tool"_s, UpdateMode::Automatic));
        QVERIFY(m_updates->info(u"tool"_s).state == UpdateState::Unknown);
        QVERIFY(checkAndWait());
        QVERIFY(waitForState(u"tool"_s, UpdateState::UpToDate));

        QVERIFY(m_service->uninstall(u"tool"_s).ok());
        QVERIFY(!m_installed->contains(u"tool"_s));
        QCOMPARE(m_settings->updateMode(u"tool"_s), UpdateMode::Manual); // the choice went with the project
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::Unknown);

        // Not asked about any more, and not installed again by an update.
        const int before = requestsMatching(u"/repos/"_s);
        QVERIFY(checkAndWait());
        QCOMPARE(requestsMatching(u"/repos/"_s), before);
        QVERIFY(!m_installed->contains(u"tool"_s));
    }

    void anInstallationByTheUserUpdatesTheState()
    {
        QVERIFY(installProject(u"tool"_s, u"Tool"_s, u"1.0.0"_s));
        offer(u"Tool"_s, u"1.1.0"_s);
        QVERIFY(writeRegistry({projectEntry(u"tool"_s, u"Tool"_s)}));
        QVERIFY(checkAndWait());
        QCOMPARE(m_updates->info(u"tool"_s).state, UpdateState::Available);

        // The user installs the newest version through the install button, not through the update.
        QSignalSpy finished(m_service.get(), &launcher::InstallService::installFinished);
        m_service->install(*registryProject(u"tool"_s));
        QVERIFY(finished.wait(30000));
        QVERIFY(finished.first().at(1).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(m_updates->info(u"tool"_s).state, UpdateState::UpToDate, 5000);
        QCOMPARE(installedVersion(u"tool"_s), u"1.1.0"_s);
    }

private:
    std::optional<launcher::Project> registryProject(const QString& id) const
    {
        for (const launcher::Project& project : m_registry->projects()) {
            if (project.id == id)
                return project;
        }
        return std::nullopt;
    }

    bool refreshAndWait(int ms)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms) {
            m_processes->refresh();
            QTest::qWait(60);
        }
        return true;
    }

    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<launcher::PathManager> m_paths;
    std::unique_ptr<TestHttpServer> m_server;
    std::unique_ptr<launcher::GitHubClient> m_client;
    std::unique_ptr<launcher::SettingsManager> m_settings;
    std::unique_ptr<launcher::RegistryManager> m_registry;
    std::unique_ptr<launcher::LibraryLayout> m_layout;
    std::unique_ptr<launcher::InstalledProjects> m_installed;
    std::unique_ptr<launcher::ReleaseManager> m_releases;
    std::unique_ptr<launcher::DownloadManager> m_downloads;
    std::unique_ptr<launcher::Installer> m_installer;
    std::unique_ptr<launcher::Uninstaller> m_uninstaller;
    std::unique_ptr<launcher::InstallService> m_service;
    std::unique_ptr<launcher::ProcessManager> m_processes;
    std::unique_ptr<launcher::ProjectUpdateManager> m_updates;

    QHash<QString, Offer> m_offers;
    QStringList m_requests;
    bool m_unavailable = false;
};

QTEST_GUILESS_MAIN(UpdateTests)
#include "UpdateTests.moc"
