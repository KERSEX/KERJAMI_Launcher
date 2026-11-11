// The whole chain behind the "Install" button: newest release, the hash to
// check against, download, verification, installation. A server on the local
// machine plays GitHub, serves a release list and the files, and is also used
// to attack the chain with wrong hashes and hostile archives.

#include "TestHttpServer.h"
#include "ZipBuilder.h"
#include "core/PathManager.h"
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
#include "security/HashVerifier.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
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

#include <windows.h>

#include <memory>

using namespace Qt::StringLiterals;
using launcher::DownloadManager;
using launcher::DownloadProblem;
using launcher::FetchOptions;
using launcher::GitHubClient;
using launcher::InstalledProjects;
using launcher::InstallError;
using launcher::InstallService;
using launcher::OperationFailure;
using launcher::PathManager;
using launcher::Project;
using launcher::ProjectStatus;
using launcher::ReleaseManager;
using Kind = launcher::OperationFailure::Kind;
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

} // namespace

class InstallServiceTests : public QObject {
    Q_OBJECT

private:
    /// What the "server" offers for one release.
    struct Offer {
        QString tag;
        QByteArray archive;
        QString assetName;
        QString digest;       ///< "" = GitHub gives none
        QString checksumName; ///< name of a checksum file, "" = none
        QByteArray checksumContent;
        qint64 announcedSize = -1; ///< -1 = real size
    };

    QString root(const QString& relative = QString()) const { return m_root->filePath(relative); }
    QString library(const QString& relative = QString()) const { return root(u"library/"_s + relative); }

    QByteArray releaseList() const
    {
        QJsonArray assets;
        const auto addAsset = [&assets](const QString& name, qint64 size, const QString& digest) {
            QJsonObject asset {{u"name"_s, name}, {u"size"_s, size}, {u"state"_s, u"uploaded"_s}};
            asset.insert(u"digest"_s,
                digest.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(u"sha256:"_s + digest));
            assets.append(asset);
        };
        addAsset(m_offer.assetName,
            m_offer.announcedSize >= 0 ? m_offer.announcedSize : m_offer.archive.size(), m_offer.digest);
        if (!m_offer.checksumName.isEmpty())
            addAsset(m_offer.checksumName, m_offer.checksumContent.size(), {});

        const QJsonObject release {{u"tag_name"_s, m_offer.tag}, {u"name"_s, u"Release"_s},
            {u"body"_s, u"Notes"_s}, {u"draft"_s, false}, {u"prerelease"_s, false},
            {u"published_at"_s, u"2026-11-01T12:00:00Z"_s}, {u"assets"_s, assets}};
        return QJsonDocument(QJsonArray {release}).toJson();
    }

    Response handle(const Request& request)
    {
        Response response;
        if (m_unavailable) {
            response.behavior = Response::Behavior::CloseWithoutAnswer;
            return response;
        }
        if (request.path.startsWith(u"/repos/KERSEX/Tool/releases"_s)) {
            response.body = releaseList();
            return response;
        }
        if (!m_notFound.isEmpty() && request.path.contains(m_notFound)) {
            response.status = 404;
            response.body = "Not Found";
            return response;
        }
        const QString prefix = u"/KERSEX/Tool/releases/download/"_s + m_offer.tag + u'/';
        if (request.path == prefix + m_offer.assetName) {
            response.body = m_offer.archive;
            if (m_stall) {
                // The archive announced in full, delivered in part: the transfer hangs until cancelled.
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

    /// An offer for a version with the given files, verified by GitHub's digest.
    Offer offerFor(const QString& version, const QList<testzip::Entry>& entries) const
    {
        Offer offer;
        offer.tag = u"v"_s + version;
        offer.archive = testzip::build(entries);
        offer.assetName = u"Tool-"_s + version + u".zip"_s;
        offer.digest = sha256(offer.archive);
        return offer;
    }

    static QList<testzip::Entry> files(const QString& version)
    {
        return {
            testzip::file(u"Tool.exe"_s, "EXE-" + version.toUtf8()),
            testzip::file(u"data/version.txt"_s, version.toUtf8()),
        };
    }

    /// Starts the installation and waits until it has ended.
    bool installAndWait()
    {
        QSignalSpy finished(m_service.get(), &InstallService::installFinished);
        m_service->install(m_project);
        if (!finished.wait(30000))
            return false;
        return finished.first().at(1).toBool();
    }

private slots:
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        m_paths = std::make_unique<PathManager>(m_root->path());
        QVERIFY(m_paths->prepare().ok());

        m_unavailable = false;
        m_notFound.clear();
        m_stall = false;
        m_offer = offerFor(u"1.0.0"_s, files(u"1.0.0"_s));

        m_server =
            std::make_unique<TestHttpServer>([this](const Request& request) { return handle(request); });
        QVERIFY(m_server->listen());

        m_client = std::make_unique<GitHubClient>(GitHubClient::Policy {{u"http"_s}, {u"127.0.0.1"_s}, true});
        FetchOptions quick;
        quick.maxAttempts = 1;
        quick.timeoutMs = 3000;

        m_layout = std::make_unique<launcher::LibraryLayout>(m_paths->libraryDir(), m_paths->tempDir());
        m_installed = std::make_unique<InstalledProjects>(m_paths->stateFile());
        m_installed->load();
        m_releases = std::make_unique<ReleaseManager>(
            *m_client, launcher::ReleaseEndpoints {m_server->url(), m_server->url()});
        m_releases->setFetchOptions(quick);
        m_downloads =
            std::make_unique<DownloadManager>(*m_client, m_paths->tempDir(), m_paths->downloadsDir());
        m_downloads->setFetchOptions(quick);
        m_installer = std::make_unique<launcher::Installer>(*m_layout, *m_installed);
        m_uninstaller = std::make_unique<launcher::Uninstaller>(*m_layout, *m_installed);
        m_service = std::make_unique<InstallService>(
            *m_client, *m_releases, *m_downloads, *m_installer, *m_uninstaller, *m_installed, *m_layout);

        m_project = {};
        m_project.id = u"tool"_s;
        m_project.name = u"The Tool"_s;
        m_project.owner = u"KERSEX"_s;
        m_project.repository = u"Tool"_s;
        m_project.assetPattern = u"Tool-*.zip"_s;
        m_project.executable = u"Tool.exe"_s;
        m_project.installDirectory = u"Tool"_s;
    }

    void cleanup()
    {
        m_service.reset();
        m_uninstaller.reset();
        m_installer.reset();
        m_downloads.reset();
        m_releases.reset();
        m_installed.reset();
        m_layout.reset();
        m_client.reset();
        m_server.reset();
        m_paths.reset();
        m_root.reset();
    }

    void installsTheNewestRelease()
    {
        QSignalSpy changes(m_service.get(), &InstallService::statusChanged);
        QVERIFY(installAndWait());

        QCOMPARE(readFile(library(u"Tool/current/Tool.exe"_s)), QByteArray("EXE-1.0.0"));
        QCOMPARE(readFile(library(u"Tool/current/data/version.txt"_s)), QByteArray("1.0.0"));
        const auto record = m_installed->get(u"tool"_s);
        QVERIFY(record.has_value());
        QCOMPARE(record->version, u"1.0.0"_s);
        QCOMPARE(record->tag, u"v1.0.0"_s);

        // No leftovers: the downloaded archive is removed, staging is gone.
        QVERIFY(!QFileInfo::exists(m_paths->downloadsDir() + u"/tool/Tool-1.0.0.zip"_s));
        QVERIFY(QDir(m_paths->tempDir()).isEmpty());

        QVERIFY(!m_service->isBusy(u"tool"_s));
        QVERIFY(!m_service->lastFailure(u"tool"_s).isFailure());
        QCOMPARE(m_service->status(u"tool"_s).stage, ProjectStatus::Stage::Idle);
        QVERIFY(changes.size() >= 4); // asking, downloading, verifying/extracting, done
    }

    void installsAReleaseThatIsASingleProgram()
    {
        // As KERS Overlay publishes: the release holds the .exe itself, and the tag has a prefix.
        m_project.assetPattern = u"KERS_Subsystems.exe"_s;
        m_project.executable = u"Overlay.exe"_s; // installed under this name, whatever the asset is called
        m_project.tagPrefix = u"KERS_SubsystemsV"_s;

        const QByteArray program = QByteArray("MZ") + QByteArray(200, 'x') + "OVERLAY-0.3.1";
        m_offer = {};
        m_offer.tag = u"KERS_SubsystemsV0.3.1"_s;
        m_offer.assetName = u"KERS_Subsystems.exe"_s;
        m_offer.archive = program; // the file the server sends
        m_offer.digest = sha256(program);

        QVERIFY(installAndWait());
        QCOMPARE(readFile(library(u"Tool/current/Overlay.exe"_s)), program);
        QCOMPARE(QDir(library(u"Tool/current"_s)).entryList(QDir::Files), QStringList {u"Overlay.exe"_s});
        const auto record = m_installed->get(u"tool"_s);
        QVERIFY(record.has_value());
        QCOMPARE(record->version, u"0.3.1"_s);
        QCOMPARE(record->tag, u"KERS_SubsystemsV0.3.1"_s);
        QCOMPARE(record->executable, u"Overlay.exe"_s);

        // The downloaded file is gone and nothing is left in temp/.
        QVERIFY(!QFileInfo::exists(m_paths->downloadsDir() + u"/tool/KERS_Subsystems.exe"_s));
        QVERIFY(QDir(m_paths->tempDir()).isEmpty());

        // A file that arrives with the right hash but is not a program is refused.
        const QByteArray notAProgram = QByteArray("not a program, ") + QByteArray(200, 'y');
        m_offer.tag = u"KERS_SubsystemsV0.3.2"_s;
        m_offer.archive = notAProgram;
        m_offer.digest = sha256(notAProgram);
        QVERIFY(!installAndWait());
        QCOMPARE(readFile(library(u"Tool/current/Overlay.exe"_s)), program); // 0.3.1 is still there
        QCOMPARE(m_installed->get(u"tool"_s)->version, u"0.3.1"_s);
        QCOMPARE(static_cast<launcher::InstallError>(m_service->lastFailure(u"tool"_s).code),
            launcher::InstallError::NotAProgram);
    }

    void reportsEachStageOnTheWay()
    {
        QList<ProjectStatus::Stage> seen;
        connect(m_service.get(), &InstallService::statusChanged, this, [&] {
            const ProjectStatus::Stage stage = m_service->status(u"tool"_s).stage;
            if (seen.isEmpty() || seen.last() != stage)
                seen.append(stage);
        });
        QVERIFY(installAndWait());

        QCOMPARE(seen.first(), ProjectStatus::Stage::FetchingRelease);
        QVERIFY(seen.contains(ProjectStatus::Stage::Downloading));
        QVERIFY(seen.contains(ProjectStatus::Stage::Verifying));
        QVERIFY(seen.contains(ProjectStatus::Stage::Extracting));
        QCOMPARE(seen.last(), ProjectStatus::Stage::Idle);
        QVERIFY(
            seen.indexOf(ProjectStatus::Stage::Downloading) < seen.indexOf(ProjectStatus::Stage::Verifying));
        QVERIFY(
            seen.indexOf(ProjectStatus::Stage::Verifying) < seen.indexOf(ProjectStatus::Stage::Extracting));
    }

    void updatesAnInstalledProjectAndKeepsABackup()
    {
        QVERIFY(installAndWait());
        m_offer = offerFor(u"1.1.0"_s, files(u"1.1.0"_s));
        QVERIFY(installAndWait());

        QCOMPARE(readFile(library(u"Tool/current/Tool.exe"_s)), QByteArray("EXE-1.1.0"));
        QCOMPARE(readFile(library(u"Tool/backups/1.0.0/Tool.exe"_s)), QByteArray("EXE-1.0.0"));
        QCOMPARE(m_installed->get(u"tool"_s)->version, u"1.1.0"_s);
    }

    void usesAChecksumFileWhenGitHubGivesNoDigest()
    {
        m_offer.digest.clear();
        m_offer.checksumName = m_offer.assetName + u".sha256"_s;
        m_offer.checksumContent = sha256(m_offer.archive).toUtf8() + "  " + m_offer.assetName.toUtf8() + "\n";
        QVERIFY(installAndWait());
        QCOMPARE(readFile(library(u"Tool/current/Tool.exe"_s)), QByteArray("EXE-1.0.0"));
    }

    void acceptsABareHashInTheFileOfTheAsset()
    {
        m_offer.digest.clear();
        m_offer.checksumName = m_offer.assetName + u".sha256"_s;
        m_offer.checksumContent = sha256(m_offer.archive).toUtf8() + "\n";
        QVERIFY(installAndWait());
    }

    // --- nothing without a trustworthy hash -----------------------------------

    void refusesAReleaseWithoutAnyHashBeforeDownloading()
    {
        m_offer.digest.clear();
        QVERIFY(!installAndWait());

        const OperationFailure failure = m_service->lastFailure(u"tool"_s);
        QCOMPARE(failure.kind, Kind::NoHash);
        // The archive was not even requested.
        for (const Request& request : m_server->requests())
            QVERIFY2(!request.path.contains(u"/download/"_s), qPrintable(request.path));
        QVERIFY(!m_installed->contains(u"tool"_s));
        QVERIFY(!QFileInfo::exists(library(u"Tool"_s)));
    }

    void refusesAChecksumFileThatNamesAnotherFile()
    {
        m_offer.digest.clear();
        m_offer.checksumName = u"SHA256SUMS"_s;
        m_offer.checksumContent = sha256(m_offer.archive).toUtf8() + "  Other-1.0.0.zip\n";
        QVERIFY(!installAndWait());
        QCOMPARE(m_service->lastFailure(u"tool"_s).kind, Kind::Checksum);
        QVERIFY(!m_installed->contains(u"tool"_s));
    }

    void refusesAMissingChecksumFile()
    {
        // The release list names the checksum file, but the server does not have it.
        m_offer.digest.clear();
        m_offer.checksumName = m_offer.assetName + u".sha256"_s;
        m_offer.checksumContent = "unused";
        m_notFound = m_offer.checksumName;

        QVERIFY(!installAndWait());
        const OperationFailure failure = m_service->lastFailure(u"tool"_s);
        QCOMPARE(failure.kind, Kind::Checksum);
        QCOMPARE(failure.httpStatus, 404);
        QVERIFY(!m_installed->contains(u"tool"_s));
    }

    void rejectsAFileWithAWrongHashAndRemovesIt()
    {
        m_offer.digest = u"0"_s.repeated(64);
        QVERIFY(!installAndWait());

        const OperationFailure failure = m_service->lastFailure(u"tool"_s);
        QCOMPARE(failure.kind, Kind::Install);
        QCOMPARE(failure.code, static_cast<int>(InstallError::HashMismatch));
        QVERIFY(!m_installed->contains(u"tool"_s));
        QVERIFY(!QFileInfo::exists(library(u"Tool"_s)));
        QVERIFY(
            !QFileInfo::exists(m_paths->downloadsDir() + u"/tool/Tool-1.0.0.zip"_s)); // the bad file is gone
    }

    void aWrongHashDoesNotTouchAnInstalledVersion()
    {
        QVERIFY(installAndWait());
        m_offer = offerFor(u"2.0.0"_s, files(u"2.0.0"_s));
        m_offer.digest = u"f"_s.repeated(64);
        QVERIFY(!installAndWait());
        QCOMPARE(readFile(library(u"Tool/current/Tool.exe"_s)), QByteArray("EXE-1.0.0"));
        QCOMPARE(m_installed->get(u"tool"_s)->version, u"1.0.0"_s);
        QVERIFY(!QFileInfo::exists(library(u"Tool/backups"_s)));
    }

    void rejectsAHostileArchiveWithACorrectHash()
    {
        // The hash is right (the attacker controls the release), the archive tries to leave the folder.
        m_offer = offerFor(
            u"1.0.0"_s, {testzip::file(u"Tool.exe"_s, "x"), testzip::file(u"../../evil.txt"_s, "evil")});
        QVERIFY(!installAndWait());

        const OperationFailure failure = m_service->lastFailure(u"tool"_s);
        QCOMPARE(failure.kind, Kind::Install);
        QCOMPARE(failure.code, static_cast<int>(InstallError::ArchiveRejected));
        QCOMPARE(failure.subcode, static_cast<int>(launcher::ArchiveValidator::Problem::UnsafePath));
        QVERIFY(!QFileInfo::exists(root(u"evil.txt"_s)));
        QVERIFY(!QFileInfo::exists(m_paths->downloadsDir() + u"/evil.txt"_s));
        QVERIFY(!m_installed->contains(u"tool"_s));
        QVERIFY(!QFileInfo::exists(library(u"Tool"_s)));
    }

    // --- failures on the way ----------------------------------------------

    void reportsAServerThatIsNotThere()
    {
        m_unavailable = true;
        QVERIFY(!installAndWait());
        const OperationFailure failure = m_service->lastFailure(u"tool"_s);
        QCOMPARE(failure.kind, Kind::Release);
        QCOMPARE(failure.code, static_cast<int>(ReleaseManager::Problem::Unreachable));
        QVERIFY(!m_service->isBusy(u"tool"_s));

        // And a later attempt works.
        m_unavailable = false;
        QVERIFY(installAndWait());
        QVERIFY(!m_service->lastFailure(u"tool"_s).isFailure());
    }

    void reportsAFileThatIsNotFound()
    {
        m_notFound = u"/download/"_s;
        QVERIFY(!installAndWait());
        const OperationFailure failure = m_service->lastFailure(u"tool"_s);
        QCOMPARE(failure.kind, Kind::Download);
        QCOMPARE(failure.code, static_cast<int>(DownloadProblem::Http));
        QCOMPARE(failure.httpStatus, 404);
    }

    void reportsAWrongAnnouncedSize()
    {
        m_offer.announcedSize = m_offer.archive.size() + 100;
        QVERIFY(!installAndWait());
        const OperationFailure failure = m_service->lastFailure(u"tool"_s);
        QCOMPARE(failure.kind, Kind::Download);
        QVERIFY(failure.code == static_cast<int>(DownloadProblem::SizeMismatch)
            || failure.code == static_cast<int>(DownloadProblem::Unreachable));
        QVERIFY(!m_installed->contains(u"tool"_s));
        QVERIFY(QDir(m_paths->tempDir()).isEmpty());
    }

    void noReleaseAtAll()
    {
        m_offer.tag = u"v1.0.0-rc.1"_s; // only a pre-release tag: not a stable release
        QVERIFY(!installAndWait());
        const OperationFailure failure = m_service->lastFailure(u"tool"_s);
        QCOMPARE(failure.kind, Kind::Release);
        QCOMPARE(failure.code, static_cast<int>(ReleaseManager::Problem::NoRelease));
    }

    // --- control -------------------------------------------------------------

    void secondRequestForTheSameProjectIsIgnored()
    {
        QSignalSpy finished(m_service.get(), &InstallService::installFinished);
        m_service->install(m_project);
        m_service->install(m_project);
        QVERIFY(m_service->isBusy(u"tool"_s));
        QCOMPARE(m_service->busyCount(), 1);
        QVERIFY(finished.wait(30000));
        QTest::qWait(200);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(m_server->requests().size(), 2); // one release list and one download
    }

    void cancelWhileAskingForTheRelease()
    {
        QSignalSpy finished(m_service.get(), &InstallService::installFinished);
        m_service->install(m_project);
        m_service->cancel(u"tool"_s);
        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished.first().at(1).toBool());
        QVERIFY(!m_service->isBusy(u"tool"_s));
        QVERIFY(!m_service->lastFailure(u"tool"_s).isFailure()); // cancelled is not a failure

        QTest::qWait(300); // the answer of the server arrives and is ignored
        QVERIFY(!m_installed->contains(u"tool"_s));
        QVERIFY(!m_service->isBusy(u"tool"_s));
        QVERIFY(QDir(m_paths->tempDir()).isEmpty());
    }

    void cancelWhileDownloading()
    {
        m_stall = true;
        m_offer.announcedSize = m_offer.archive.size() + 300 * 1024;
        QSignalSpy finished(m_service.get(), &InstallService::installFinished);
        m_service->install(m_project);
        QTRY_COMPARE_WITH_TIMEOUT(
            m_service->status(u"tool"_s).stage, ProjectStatus::Stage::Downloading, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(m_service->status(u"tool"_s).done > 0, 10000);

        m_service->cancel(u"tool"_s);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        QVERIFY(!finished.first().at(1).toBool());
        QVERIFY(!m_service->lastFailure(u"tool"_s).isFailure());
        QVERIFY(!m_service->isBusy(u"tool"_s));
        QVERIFY(QDir(m_paths->tempDir()).isEmpty()); // the partial file is gone
        QVERIFY(!m_installed->contains(u"tool"_s));
    }

    // --- preparing an installation (for updates that wait) ----------------------------

    void prepareOnlyStopsAfterTheVerificationAndKeepsTheArchive()
    {
        QSignalSpy prepared(m_service.get(), &InstallService::prepared);
        QSignalSpy finished(m_service.get(), &InstallService::installFinished);
        m_service->install(m_project, InstallService::Mode::PrepareOnly);
        QVERIFY(prepared.wait(30000));

        QCOMPARE(prepared.first().first().toString(), u"tool"_s);
        QCOMPARE(m_service->status(u"tool"_s).stage, ProjectStatus::Stage::Prepared);
        QVERIFY(m_service->isBusy(u"tool"_s));
        QCOMPARE(finished.size(), 0);

        // Nothing is installed; the verified archive waits in downloads/.
        QVERIFY(!m_installed->contains(u"tool"_s));
        QVERIFY(!QFileInfo::exists(library(u"Tool"_s)));
        QCOMPARE(readFile(m_paths->downloadsDir() + u"/tool/Tool-1.0.0.zip"_s), m_offer.archive);

        // While it waits, the project cannot be removed or installed a second time.
        QCOMPARE(m_service->uninstall(u"tool"_s).error, launcher::UninstallError::Busy);
        m_service->install(m_project);
        QCOMPARE(m_service->busyCount(), 1);

        // Going on installs what was prepared, without asking GitHub or downloading again.
        const int requests = m_server->requestCount();
        m_service->installPrepared(u"tool"_s);
        QVERIFY(finished.wait(30000));
        QVERIFY(finished.first().at(1).toBool());
        QCOMPARE(readFile(library(u"Tool/current/Tool.exe"_s)), QByteArray("EXE-1.0.0"));
        QCOMPARE(m_server->requestCount(), requests);
        QVERIFY(!QFileInfo::exists(m_paths->downloadsDir() + u"/tool/Tool-1.0.0.zip"_s));
    }

    void prepareOnlyRejectsABadFileAtOnce()
    {
        m_offer.digest = u"0"_s.repeated(64);
        QSignalSpy prepared(m_service.get(), &InstallService::prepared);
        QSignalSpy finished(m_service.get(), &InstallService::installFinished);
        m_service->install(m_project, InstallService::Mode::PrepareOnly);
        QVERIFY(finished.wait(30000));

        QCOMPARE(prepared.size(), 0);
        QVERIFY(!finished.first().at(1).toBool());
        const OperationFailure failure = m_service->lastFailure(u"tool"_s);
        QCOMPARE(failure.kind, Kind::Install);
        QCOMPARE(failure.code, static_cast<int>(InstallError::HashMismatch));
        QVERIFY(!QFileInfo::exists(m_paths->downloadsDir() + u"/tool/Tool-1.0.0.zip"_s));
        QVERIFY(!m_installed->contains(u"tool"_s));
        QVERIFY(QDir(m_paths->tempDir()).isEmpty());
    }

    void cancelDiscardsAPreparedInstallation()
    {
        QSignalSpy prepared(m_service.get(), &InstallService::prepared);
        QSignalSpy finished(m_service.get(), &InstallService::installFinished);
        m_service->install(m_project, InstallService::Mode::PrepareOnly);
        QVERIFY(prepared.wait(30000));

        m_service->cancel(u"tool"_s);
        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished.first().at(1).toBool());
        QVERIFY(!m_service->lastFailure(u"tool"_s).isFailure());
        QVERIFY(!m_service->isBusy(u"tool"_s));
        QVERIFY(!QFileInfo::exists(m_paths->downloadsDir() + u"/tool/Tool-1.0.0.zip"_s));
        QVERIFY(!m_installed->contains(u"tool"_s));
    }

    void installPreparedDoesNothingOutsideThePreparedStage()
    {
        m_service->installPrepared(u"tool"_s); // nothing running
        m_service->installPrepared(u"nobody"_s);
        QVERIFY(!m_service->isBusy(u"tool"_s));

        QSignalSpy finished(m_service.get(), &InstallService::installFinished);
        m_service->install(m_project, InstallService::Mode::PrepareOnly);
        m_service->installPrepared(u"tool"_s); // still asking GitHub: not prepared yet
        QCOMPARE(m_service->status(u"tool"_s).stage, ProjectStatus::Stage::FetchingRelease);
        m_service->cancel(u"tool"_s);
        QCOMPARE(finished.size(), 1);
        QTest::qWait(300);
        QVERIFY(!m_installed->contains(u"tool"_s));
    }

    void cancelDuringTheEarlyVerificationIsNoFailure()
    {
        // A larger archive, so that the verification takes a moment.
        m_offer = offerFor(u"1.0.0"_s, {testzip::file(u"Tool.exe"_s, QByteArray(40'000'000, 'x'), false)});
        QSignalSpy prepared(m_service.get(), &InstallService::prepared);
        QSignalSpy finished(m_service.get(), &InstallService::installFinished);
        m_service->install(m_project, InstallService::Mode::PrepareOnly);
        QTRY_COMPARE_WITH_TIMEOUT(m_service->status(u"tool"_s).stage, ProjectStatus::Stage::Verifying, 30000);

        m_service->cancel(u"tool"_s);
        QVERIFY(finished.wait(30000));
        QVERIFY(!finished.first().at(1).toBool());
        QVERIFY(!m_service->lastFailure(u"tool"_s).isFailure());
        QCOMPARE(prepared.size(), 0);
        QVERIFY(!QFileInfo::exists(m_paths->downloadsDir() + u"/tool/Tool-1.0.0.zip"_s));
    }

    void startUsesTheConfiguredStarter()
    {
        QString asked;
        m_service->setStarter([&asked](const QString& id) {
            asked = id;
            launcher::StartResult result;
            result.error = launcher::StartError::AlreadyRunning;
            return result;
        });
        QVERIFY(installAndWait());
        const auto result = m_service->start(u"tool"_s);
        QCOMPARE(asked, u"tool"_s);
        QCOMPARE(result.error, launcher::StartError::AlreadyRunning);
        QCOMPARE(m_service->lastFailure(u"tool"_s).kind, Kind::Start);
        QCOMPARE(
            m_service->lastFailure(u"tool"_s).code, static_cast<int>(launcher::StartError::AlreadyRunning));
    }

    // --- uninstall and start --------------------------------------------------

    void uninstallRemovesTheProject()
    {
        QVERIFY(installAndWait());
        const auto result = m_service->uninstall(u"tool"_s);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QVERIFY(!m_installed->contains(u"tool"_s));
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(library(u"Tool"_s)), 10000);
        QVERIFY(!m_service->lastFailure(u"tool"_s).isFailure());
    }

    /// Starts the installed program the way the launcher does, watches it for a few seconds, closes it
    /// (politely first, by force if it does not go) and reports what it saw. Part of the live test.
    void liveStartAndStop(const PathManager& paths, const launcher::LibraryLayout& layout,
        InstalledProjects& installed, const Project& project, const launcher::InstalledProject& record)
    {
        Q_UNUSED(paths);
        launcher::ProcessManager processes(layout, installed);
        processes.setPolling(false);

        const QString exe = QDir::toNativeSeparators(layout.executablePath(record));
        const launcher::StartResult started = processes.start(project.id);
        QVERIFY2(started.ok(), qPrintable(started.detail));
        QVERIFY(started.processId > 0);
        QVERIFY(installed.get(project.id)->lastStartedAt.isValid()); // the launcher noted the start
        QVERIFY(processes.isRunningNow(project.id));

        // It is the installed file that runs: the image of the process is the file in current/.
        {
            HANDLE process =
                OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(started.processId));
            QVERIFY(process != nullptr);
            wchar_t image[4096];
            DWORD size = static_cast<DWORD>(sizeof(image) / sizeof(image[0]));
            const bool got = QueryFullProcessImageNameW(process, 0, image, &size);
            CloseHandle(process);
            QVERIFY(got);
            QCOMPARE(QDir::cleanPath(QString::fromWCharArray(image, static_cast<qsizetype>(size))).toLower(),
                QDir::cleanPath(exe).toLower());
        }

        // Ten seconds: is it still there, does it show a window, did it end by itself?
        struct WindowCount {
            DWORD pid;
            int total = 0;
            int visible = 0;
        } windows {static_cast<DWORD>(started.processId)};
        const auto countWindows = [&windows] {
            windows.total = windows.visible = 0;
            EnumWindows(
                [](HWND window, LPARAM parameter) -> BOOL {
                    auto* counts = reinterpret_cast<WindowCount*>(parameter);
                    DWORD owner = 0;
                    GetWindowThreadProcessId(window, &owner);
                    if (owner == counts->pid) {
                        ++counts->total;
                        if (IsWindowVisible(window))
                            ++counts->visible;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&windows));
        };

        int secondsAlive = 0;
        int mostVisible = 0;
        for (int i = 0; i < 20; ++i) {
            QTest::qWait(500);
            processes.refresh();
            countWindows();
            mostVisible = qMax(mostVisible, windows.visible);
            if (!processes.isRunningNow(project.id))
                break;
            secondsAlive = (i + 1) / 2;
        }
        const bool aliveAfterWatching = processes.isRunningNow(project.id);
        qInfo() << "KERS Overlay: process" << started.processId
                << (aliveAfterWatching ? "still running" : "ended") << "after" << secondsAlive << "s, at most"
                << mostVisible << "visible window(s)";
        if (const std::optional<launcher::ProcessExit> exit = processes.lastExit(project.id))
            qInfo() << "KERS Overlay ended by itself with exit code" << exit->exitCode << "after"
                    << exit->runSeconds << "s";

        // What the program wrote next to itself.
        const QStringList written = QDir(layout.currentDir(project.installDirectory))
                                        .entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
        qInfo() << "Files in current/ now:" << written;
        // One level deeper, with sizes: what the program keeps next to itself (settings? caches?).
        for (const QString& name : written) {
            const QFileInfo entry(layout.currentDir(project.installDirectory) + u'/' + name);
            if (!entry.isDir())
                continue;
            QStringList inside;
            QDirIterator it(entry.filePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext() && inside.size() < 30) {
                it.next();
                inside.append(entry.dir().relativeFilePath(it.filePath()) + u" ("_s
                    + QString::number(it.fileInfo().size()) + u" bytes)"_s);
            }
            qInfo() << name + u"/:"_s << inside;
        }

        // Closing: ask the windows to close, then wait; whatever is still there is ended with its children.
        if (aliveAfterWatching) {
            EnumWindows(
                [](HWND window, LPARAM parameter) -> BOOL {
                    DWORD owner = 0;
                    GetWindowThreadProcessId(window, &owner);
                    if (owner == static_cast<DWORD>(parameter))
                        PostMessageW(window, WM_CLOSE, 0, 0);
                    return TRUE;
                },
                static_cast<LPARAM>(started.processId));
            for (int i = 0; i < 20 && processes.isRunningNow(project.id); ++i)
                QTest::qWait(500);
            if (processes.isRunningNow(project.id)) {
                qInfo() << "KERS Overlay did not close when asked; it is ended by force";
                QProcess::execute(
                    u"taskkill"_s, {u"/PID"_s, QString::number(started.processId), u"/T"_s, u"/F"_s});
            }
        }
        QTRY_VERIFY_WITH_TIMEOUT(!processes.isRunningNow(project.id), 20000);
        // The files of the program are free again, so that the uninstallation can delete them.
        QTest::qWait(500);
    }

    /// The real thing, once: the first project of the built-in list is installed from GitHub and removed
    /// again. It downloads the release file (about 56 MB), so it runs only when asked for with its own
    /// switch, apart from LAUNCHER_LIVE_TESTS, which reads metadata only.
    void liveInstallOfKersOverlay()
    {
        if (!qEnvironmentVariableIsSet("LAUNCHER_LIVE_DOWNLOAD_TESTS"))
            QSKIP("Set LAUNCHER_LIVE_DOWNLOAD_TESTS=1 to download the release of KERS Overlay (about 56 MB) "
                  "from GitHub, install it into a temporary folder and remove it again.");

        // The project exactly as the list built into the program has it.
        launcher::RegistryManager list;
        QVERIFY(list.loadBuiltIn());
        Project project;
        for (const Project& candidate : list.projects()) {
            if (candidate.id == u"kers-overlay"_s)
                project = candidate;
        }
        QVERIFY2(!project.id.isEmpty(), "KERS Overlay is not in the built-in list");

        QTemporaryDir root;
        QVERIFY(root.isValid());
        PathManager paths(root.filePath(u"app"_s));
        QVERIFY(QDir().mkpath(paths.root()));
        QVERIFY(paths.prepare().ok());

        // The production client: HTTPS to GitHub only, as in the application.
        GitHubClient client(GitHubClient::Policy::production());
        launcher::LibraryLayout layout(paths.libraryDir(), paths.tempDir());
        InstalledProjects installed(paths.stateFile());
        installed.load();
        ReleaseManager releases(client);
        DownloadManager downloads(client, paths.tempDir(), paths.downloadsDir());
        launcher::Installer installer(layout, installed);
        launcher::Uninstaller uninstaller(layout, installed);
        InstallService service(client, releases, downloads, installer, uninstaller, installed, layout);

        // Progress, so that a long download is visible in the log.
        ProjectStatus::Stage lastStage = ProjectStatus::Stage::Idle;
        connect(&service, &InstallService::statusChanged, this, [&] {
            const ProjectStatus status = service.status(project.id);
            if (status.stage != lastStage) {
                lastStage = status.stage;
                qInfo() << "Stage" << static_cast<int>(status.stage) << "of" << status.total << "bytes";
            }
        });

        QElapsedTimer clock;
        clock.start();
        QSignalSpy finished(&service, &InstallService::installFinished);
        service.install(project);
        QVERIFY2(finished.wait(300000), "no end of the installation within five minutes");
        const OperationFailure failure = service.lastFailure(project.id);
        QVERIFY2(finished.first().at(1).toBool(),
            qPrintable(
                u"installation failed: kind %1, code %2, HTTP %3, %4"_s.arg(static_cast<int>(failure.kind))
                    .arg(failure.code)
                    .arg(failure.httpStatus)
                    .arg(failure.detail)));

        // What was installed: the version, the file, and that it is the file GitHub announced.
        const std::optional<launcher::InstalledProject> record = installed.get(project.id);
        QVERIFY(record.has_value());
        QVERIFY(record->tag.startsWith(project.tagPrefix));
        QCOMPARE(record->executable, project.executable);
        const std::optional<launcher::ReleaseSelection> selection = releases.latest(project.id);
        QVERIFY(selection.has_value());
        QCOMPARE(record->version, selection->release.version.toString());

        const QString exe = layout.executablePath(*record);
        QVERIFY(QFileInfo::exists(exe));
        QCOMPARE(QFileInfo(exe).size(), selection->asset.size);
        QVERIFY2(!selection->asset.sha256.isEmpty(), "GitHub announced no digest");
        QCOMPARE(launcher::HashVerifier::sha256File(exe), selection->asset.sha256);

        // A Windows program: "MZ", and a "PE" header where the DOS header points to.
        QFile file(exe);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray head = file.read(4096);
        QVERIFY(head.startsWith("MZ"));
        const int peOffset =
            static_cast<unsigned char>(head.at(0x3C)) | (static_cast<unsigned char>(head.at(0x3D)) << 8);
        QVERIFY(peOffset > 0 && peOffset + 4 <= head.size());
        QCOMPARE(head.mid(peOffset, 4), QByteArray("PE\0\0", 4));
        file.close();

        // Nothing is left over: no download, no staging folder.
        QVERIFY(QDir(paths.downloadsDir()).isEmpty());
        QVERIFY2(QDir(paths.tempDir()).isEmpty(), qPrintable(QDir(paths.tempDir()).entryList().join(u',')));
        qInfo() << "Installed" << record->version << "(" << QFileInfo(exe).size() << "bytes ) in"
                << clock.elapsed() / 1000.0 << "seconds";

        // Started once, if that is asked for as well: it runs the downloaded program for a few seconds.
        if (qEnvironmentVariableIsSet("LAUNCHER_LIVE_START_TESTS"))
            liveStartAndStop(paths, layout, installed, project, *record);

        // And away again.
        const launcher::UninstallResult removed = service.uninstall(project.id);
        QVERIFY2(removed.ok(), qPrintable(removed.detail));
        QVERIFY(!installed.contains(project.id));
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(layout.projectDir(project.installDirectory)), 20000);
        QTRY_VERIFY_WITH_TIMEOUT(QDir(paths.tempDir()).isEmpty(), 20000);
    }

    void uninstallIsRefusedWhileInstalling()
    {
        QSignalSpy finished(m_service.get(), &InstallService::installFinished);
        m_service->install(m_project);
        const auto result = m_service->uninstall(u"tool"_s);
        QCOMPARE(result.error, launcher::UninstallError::Busy);
        QCOMPARE(m_service->lastFailure(u"tool"_s).kind, Kind::Uninstall);
        QVERIFY(finished.wait(30000));
    }

    void startingANotInstalledProjectFails()
    {
        const auto result = m_service->start(u"tool"_s);
        QCOMPARE(result.error, launcher::StartError::NotInstalled);
        QCOMPARE(m_service->lastFailure(u"tool"_s).kind, Kind::Start);
        m_service->dismissFailure(u"tool"_s);
        QVERIFY(!m_service->lastFailure(u"tool"_s).isFailure());
    }

    void aNewAttemptClearsTheOldFailure()
    {
        m_offer.digest.clear();
        QVERIFY(!installAndWait());
        QVERIFY(m_service->lastFailure(u"tool"_s).isFailure());

        m_offer.digest = sha256(m_offer.archive);
        QSignalSpy finished(m_service.get(), &InstallService::installFinished);
        m_service->install(m_project);
        QVERIFY(!m_service->lastFailure(u"tool"_s).isFailure()); // cleared when the new attempt starts
        QVERIFY(finished.wait(30000));
        QVERIFY(finished.first().at(1).toBool());
    }

private:
    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<PathManager> m_paths;
    std::unique_ptr<TestHttpServer> m_server;
    std::unique_ptr<GitHubClient> m_client;
    std::unique_ptr<launcher::LibraryLayout> m_layout;
    std::unique_ptr<InstalledProjects> m_installed;
    std::unique_ptr<ReleaseManager> m_releases;
    std::unique_ptr<DownloadManager> m_downloads;
    std::unique_ptr<launcher::Installer> m_installer;
    std::unique_ptr<launcher::Uninstaller> m_uninstaller;
    std::unique_ptr<InstallService> m_service;
    Project m_project;
    Offer m_offer;
    bool m_unavailable = false;
    QString m_notFound; ///< Requests whose path contains this get a 404.
    bool m_stall = false;
};

QTEST_GUILESS_MAIN(InstallServiceTests)
#include "InstallServiceTests.moc"
