// LauncherUpdater.exe, the real program: it waits for the launcher to end,
// replaces the program files from the staged release, starts the launcher
// again, and leaves a report. "The launcher" is a copy of SleepyApp; the
// program that is started again is told apart by the size of its file.

#include "SleepyProject.h"
#include "app/Branding.h"
#include "core/JsonFile.h"
#include "core/PathManager.h"
#include "security/HashVerifier.h"
#include "updater/UpdateManifest.h"
#include "updater/UpdaterProtocol.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <windows.h>

#include <map>
#include <memory>

using namespace Qt::StringLiterals;
using launcher::UpdateManifest;
using launcher::UpdateReport;
using launcher::UpdaterJob;
namespace branding = launcher::branding;

namespace {

using Files = std::map<QString, QByteArray>;

QString sha256(const QByteArray& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool writeFile(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

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

Files snapshot(const QString& root, const QStringList& skipTop)
{
    Files files;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = QDir(root).relativeFilePath(it.next());
        if (!skipTop.contains(path.section(u'/', 0, 0), Qt::CaseInsensitive))
            files[path] = readFile(root + u'/' + path);
    }
    return files;
}

} // namespace

class UpdaterTests : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QByteArray m_oldLauncher;
    QByteArray m_newLauncher;

    QString root() const { return m_dir->filePath(u"app"_s); }
    QString staging() const { return root() + u"/temp/update-2.0.0"_s; }
    QString jobDir() const { return root() + u"/temp/updater-2.0.0"_s; }
    launcher::PathManager paths() const { return launcher::PathManager(root()); }

    static QString updaterExe()
    {
        return QDir::cleanPath(
            QCoreApplication::applicationDirPath() + u"/../bin/"_s + branding::updaterFileName());
    }

    QStringList startedLog() const
    {
        QStringList lines;
        for (const QString& line : QString::fromUtf8(readFile(root() + u"/started.log"_s)).split(u'\n')) {
            if (!line.trimmed().isEmpty())
                lines.append(line.trimmed());
        }
        return lines;
    }

    /// The installed version, the user's data around it, and the next version in staging.
    void setUpWorld(bool breakManifestPath = false)
    {
        m_oldLauncher = readFile(sleepy::appPath());
        m_newLauncher = m_oldLauncher + "NEW-2.0.0";
        QVERIFY(!m_oldLauncher.isEmpty());

        QVERIFY(writeFile(root() + u"/"_s + branding::executableFileName(), m_oldLauncher));
        QVERIFY(writeFile(root() + u"/"_s + branding::updaterFileName(), "old updater"));
        QVERIFY(writeFile(root() + u"/extra.dll"_s, "old extra"));
        QVERIFY(writeFile(root() + u"/launcher/settings.json"_s, "{\"a\":1}"));
        QVERIFY(writeFile(root() + u"/library/tool/current/Tool.exe"_s, "project"));
        QVERIFY(writeFile(root() + u"/downloads/tool/Tool.zip"_s, "download"));
        QVERIFY(writeFile(root() + u"/logs/launcher.log"_s, "log"));

        Files fresh {{branding::executableFileName(), m_newLauncher},
            {branding::updaterFileName(), "NEW updater"}, {u"extra.dll"_s, "NEW extra"}};
        UpdateManifest manifest;
        manifest.schemaVersion = 1;
        manifest.product = branding::productName();
        manifest.version = u"2.0.0"_s;
        manifest.launcher = branding::executableFileName();
        manifest.updater = branding::updaterFileName();
        for (const auto& [path, content] : fresh) {
            QVERIFY(writeFile(staging() + u'/' + path, content));
            manifest.files.append({path, content.size(), sha256(content)});
        }
        QByteArray json = launcher::UpdateManifestFormat::toJson(manifest);
        if (breakManifestPath)
            json.replace("\"extra.dll\"", "\"../evil.dll\"");
        QVERIFY(writeFile(staging() + u"/update-manifest.json"_s, json));
    }

    Files userData() const
    {
        Files files = snapshot(root(),
            {branding::executableFileName(), branding::updaterFileName(), u"extra.dll"_s, u"temp"_s,
                u"updater"_s, u"started.log"_s, u"stop"_s});
        files.erase(u"logs/updater.log"_s); // the updater's own log: it writes it, nobody else's data
        return files;
    }

    UpdaterJob makeJob(qint64 pid) const
    {
        UpdaterJob job;
        job.rootDir = root();
        job.stagingDir = staging();
        job.fromVersion = u"1.0.0"_s;
        job.toVersion = u"2.0.0"_s;
        job.launcherFile = branding::executableFileName();
        job.updaterFile = branding::updaterFileName();
        job.product = branding::productName();
        job.launcherProcessId = pid;
        job.waitTimeoutMs = 20000;
        return job;
    }

    /// Starts the updater the way the launcher does: its own copy, in temp/, with a job file.
    void startUpdater(QProcess& process, const UpdaterJob& job)
    {
        QVERIFY(QDir().mkpath(jobDir()));
        const QString copy = jobDir() + u'/' + branding::updaterFileName();
        QFile::remove(copy);
        QVERIFY(QFile::copy(updaterExe(), copy));
        const QString jobFile = jobDir() + u"/job.json"_s;
        QVERIFY(launcher::JsonFile::saveAtomic(jobFile, job.toJson()));
        process.start(copy, {u"--job"_s, jobFile, u"--quiet"_s});
        QVERIFY(process.waitForStarted(10000));
    }

    int runUpdater(const UpdaterJob& job)
    {
        QProcess process;
        startUpdater(process, job);
        if (!process.waitForFinished(60000)) {
            process.kill();
            return -1;
        }
        return process.exitCode();
    }

    std::optional<UpdateReport> report() const
    {
        const auto file = launcher::JsonFile::load(paths().updateReportFile());
        if (file.status != launcher::JsonFile::Status::Ok)
            return std::nullopt;
        return UpdateReport::fromJson(file.object);
    }

    /// The old launcher: a real running program in the launcher's folder.
    bool startOldLauncher(QProcess& process)
    {
        process.start(root() + u"/"_s + branding::executableFileName(), {});
        return process.waitForStarted(10000);
    }

    void stopLauncher() { QVERIFY(writeFile(root() + u"/stop"_s, "1")); }

private slots:
    void initTestCase()
    {
        QVERIFY2(QFileInfo::exists(updaterExe()), qPrintable(updaterExe()));
        QVERIFY2(QFileInfo::exists(sleepy::appPath()), "SleepyApp.exe is not built");
    }

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
    }

    void cleanup()
    {
        if (m_dir)
            stopLauncher(); // no program of a test outlives it
        QTest::qWait(300);
        m_dir.reset();
    }

    // === the normal way ================================================================

    void replacesTheProgramAfterTheLauncherHasEnded()
    {
        setUpWorld();
        const Files dataBefore = userData();
        QProcess oldLauncher;
        QVERIFY(startOldLauncher(oldLauncher));
        QTRY_VERIFY_WITH_TIMEOUT(startedLog().size() == 1, 5000);

        QProcess updater;
        startUpdater(updater, makeJob(oldLauncher.processId()));

        // The updater waits as long as the launcher runs: nothing happens.
        QTest::qWait(1500);
        QCOMPARE(updater.state(), QProcess::Running);
        QCOMPARE(readFile(root() + u"/"_s + branding::executableFileName()), m_oldLauncher);
        QCOMPARE(readFile(root() + u"/extra.dll"_s), QByteArray("old extra"));
        QVERIFY(!report().has_value());
        QVERIFY(QFileInfo::exists(paths().updaterLockFile())); // an update is under way

        // The launcher ends: now the files are replaced and the launcher is started again.
        stopLauncher();
        QVERIFY(oldLauncher.waitForFinished(10000));
        QVERIFY(updater.waitForFinished(60000));
        QCOMPARE(updater.exitCode(), 0);

        QCOMPARE(readFile(root() + u"/"_s + branding::executableFileName()), m_newLauncher);
        QCOMPARE(readFile(root() + u"/"_s + branding::updaterFileName()),
            QByteArray("NEW updater")); // the updater replaced itself too
        QCOMPARE(readFile(root() + u"/extra.dll"_s), QByteArray("NEW extra"));
        QCOMPARE(userData(), dataBefore); // the user's data, byte for byte

        const std::optional<UpdateReport> result = report();
        QVERIFY(result.has_value());
        QCOMPARE(result->status, UpdateReport::Status::Success);
        QCOMPARE(result->fromVersion, u"1.0.0"_s);
        QCOMPARE(result->toVersion, u"2.0.0"_s);
        QVERIFY(result->launcherStarted);
        QVERIFY(!result->backupDir.isEmpty());
        QCOMPARE(readFile(root() + u'/' + result->backupDir + u"/extra.dll"_s), QByteArray("old extra"));
        QCOMPARE(readFile(root() + u'/' + result->backupDir + u'/' + branding::executableFileName()),
            m_oldLauncher);

        // The new launcher was started, not the old one.
        QTRY_COMPARE_WITH_TIMEOUT(startedLog().size(), 2, 10000);
        QCOMPARE(startedLog().last(), QString::number(m_newLauncher.size()));

        // Staging is gone, the journal is gone, the update lock is released.
        QVERIFY2(!QFileInfo::exists(staging()),
            qPrintable(QDir(staging()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).join(u',')));
        QVERIFY(!QFileInfo::exists(paths().updateJournalFile()));
        QLockFile lock(paths().updaterLockFile());
        QVERIFY(lock.tryLock(0));
    }

    void doesNotWaitForAnUnrelatedProcessThatReusesTheId()
    {
        // The process id in the job belongs to another program now: the updater only waits for the launcher.
        setUpWorld();
        const QString elsewhere = m_dir->filePath(u"elsewhere"_s);
        QVERIFY(QDir().mkpath(elsewhere));
        QVERIFY(QFile::copy(sleepy::appPath(), elsewhere + u"/Other.exe"_s));
        QProcess other;
        other.start(elsewhere + u"/Other.exe"_s, {});
        QVERIFY(other.waitForStarted(10000));

        const int code = runUpdater(makeJob(other.processId()));
        QCOMPARE(code, 0);
        QCOMPARE(readFile(root() + u"/extra.dll"_s), QByteArray("NEW extra"));
        sleepy::stop(elsewhere);
        other.waitForFinished(10000);
    }

    void aLauncherThatIsAlreadyGoneIsNoProblem()
    {
        // No process with this id exists: there is nothing to wait for.
        setUpWorld();
        QCOMPARE(runUpdater(makeJob(2'000'000'000)), 0);
        QCOMPARE(readFile(root() + u"/extra.dll"_s), QByteArray("NEW extra"));
    }

    // === when something goes wrong =========================================================

    void rollsBackWhenAFileIsInUse()
    {
        setUpWorld();
        const Files dataBefore = userData();
        stopLauncher(); // the old launcher is not running; a file of the program is held open
        FileLock lock(root() + u"/extra.dll"_s);
        QVERIFY(lock.isLocked());

        const int code = runUpdater(makeJob(0));
        QCOMPARE(code, 1);

        // The old version, completely.
        QCOMPARE(readFile(root() + u"/"_s + branding::executableFileName()), m_oldLauncher);
        QCOMPARE(readFile(root() + u"/"_s + branding::updaterFileName()), QByteArray("old updater"));
        QCOMPARE(readFile(root() + u"/extra.dll"_s), QByteArray("old extra"));
        QCOMPARE(userData(), dataBefore);
        QVERIFY(!QFileInfo::exists(paths().updateJournalFile()));

        const std::optional<UpdateReport> result = report();
        QVERIFY(result.has_value());
        QCOMPARE(result->status, UpdateReport::Status::RolledBack);
        QCOMPARE(result->failedPath, u"extra.dll"_s);
        // The old launcher is started again, so the user is not left without the program.
        QVERIFY(result->launcherStarted);
        QTRY_COMPARE_WITH_TIMEOUT(startedLog().size(), 1, 10000);
        QCOMPARE(startedLog().last(), QString::number(m_oldLauncher.size()));
    }

    void givesUpWhenTheLauncherDoesNotClose()
    {
        setUpWorld();
        QProcess oldLauncher;
        QVERIFY(startOldLauncher(oldLauncher));
        QTRY_VERIFY_WITH_TIMEOUT(startedLog().size() == 1, 5000);

        UpdaterJob job = makeJob(oldLauncher.processId());
        job.waitTimeoutMs = 1500;
        QCOMPARE(runUpdater(job), 2);

        QCOMPARE(readFile(root() + u"/extra.dll"_s), QByteArray("old extra")); // nothing was touched
        QCOMPARE(readFile(root() + u"/"_s + branding::executableFileName()), m_oldLauncher);
        const std::optional<UpdateReport> result = report();
        QVERIFY(result.has_value());
        QCOMPARE(result->status, UpdateReport::Status::NotApplied);
        QVERIFY(result->detail.contains(u"did not close"_s));
        QVERIFY(!result->launcherStarted); // it is still running; a second one is not started
        QCOMPARE(startedLog().size(), 1);

        stopLauncher();
        oldLauncher.waitForFinished(10000);
    }

    void refusesAManifestThatReachesOutside()
    {
        setUpWorld(/*breakManifestPath*/ true);
        const Files dataBefore = userData();
        QCOMPARE(runUpdater(makeJob(0)), 2);

        QCOMPARE(readFile(root() + u"/extra.dll"_s), QByteArray("old extra"));
        QCOMPARE(readFile(root() + u"/"_s + branding::executableFileName()), m_oldLauncher);
        QCOMPARE(userData(), dataBefore);
        QVERIFY(!QFileInfo::exists(m_dir->filePath(u"evil.dll"_s)));

        const std::optional<UpdateReport> result = report();
        QVERIFY(result.has_value());
        QCOMPARE(result->status, UpdateReport::Status::NotApplied);
        QVERIFY(result->detail.contains(u"evil"_s));
        QVERIFY(result->launcherStarted); // nothing was changed: the old launcher starts and reports it
    }

    void refusesAStagedFileThatWasTamperedWith()
    {
        setUpWorld();
        QVERIFY(writeFile(staging() + u"/extra.dll"_s, "TAMPERED"));
        QCOMPARE(runUpdater(makeJob(0)), 2);
        QCOMPARE(readFile(root() + u"/extra.dll"_s), QByteArray("old extra"));
        QCOMPARE(readFile(root() + u"/"_s + branding::executableFileName()), m_oldLauncher);
        QCOMPARE(report()->status, UpdateReport::Status::NotApplied);
    }

    void refusesAManifestForAnotherVersionOrProduct()
    {
        setUpWorld();
        UpdaterJob job = makeJob(0);
        job.toVersion = u"3.0.0"_s; // the manifest says 2.0.0
        QCOMPARE(runUpdater(job), 2);
        QCOMPARE(readFile(root() + u"/extra.dll"_s), QByteArray("old extra"));

        UpdaterJob other = makeJob(0);
        other.product = u"SomethingElse"_s;
        QVERIFY(QFile::remove(paths().updateReportFile()));
        QCOMPARE(runUpdater(other), 2);
        QCOMPARE(readFile(root() + u"/extra.dll"_s), QByteArray("old extra"));
    }

    void refusesAStagingFolderOutsideTheLauncherFolder()
    {
        setUpWorld();
        UpdaterJob job = makeJob(0);
        const QString outside = m_dir->filePath(u"outside-staging"_s);
        QVERIFY(QDir().mkpath(outside));
        job.stagingDir = outside;
        QCOMPARE(runUpdater(job), 2);
        QCOMPARE(readFile(root() + u"/extra.dll"_s), QByteArray("old extra"));
    }

    void doesNothingWhileAnotherUpdateIsRunning()
    {
        setUpWorld();
        QDir().mkpath(paths().updaterDir());
        QLockFile other(paths().updaterLockFile());
        QVERIFY(other.tryLock(0));
        QCOMPARE(runUpdater(makeJob(0)), 2);
        QCOMPARE(readFile(root() + u"/extra.dll"_s), QByteArray("old extra"));
    }

    void doesNotStartTheLauncherIfAskedNotTo()
    {
        setUpWorld();
        UpdaterJob job = makeJob(0);
        job.startLauncherAfter = false;
        QCOMPARE(runUpdater(job), 0);
        QVERIFY(!report()->launcherStarted);
        QTest::qWait(500);
        QVERIFY(!QFileInfo::exists(root() + u"/started.log"_s));
    }

    // === a broken job ================================================================================

    void withoutAJobItDoesNothing()
    {
        QProcess process;
        process.start(updaterExe(), {u"--quiet"_s});
        QVERIFY(process.waitForFinished(20000));
        QCOMPARE(process.exitCode(), 4);

        QProcess garbage;
        QVERIFY(writeFile(m_dir->filePath(u"job.json"_s), "{ not json"));
        garbage.start(updaterExe(), {u"--job"_s, m_dir->filePath(u"job.json"_s), u"--quiet"_s});
        QVERIFY(garbage.waitForFinished(20000));
        QCOMPARE(garbage.exitCode(), 4);

        QProcess missing;
        missing.start(updaterExe(), {u"--job"_s, m_dir->filePath(u"no-such-job.json"_s), u"--quiet"_s});
        QVERIFY(missing.waitForFinished(20000));
        QCOMPARE(missing.exitCode(), 4);
    }

    void protocolRoundTrip()
    {
        UpdaterJob job = makeJob(1234);
        job.startLauncherAfter = false;
        const auto parsed = UpdaterJob::fromJson(job.toJson());
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->rootDir, job.rootDir);
        QCOMPARE(parsed->launcherProcessId, qint64(1234));
        QVERIFY(!parsed->startLauncherAfter);
        QVERIFY(!UpdaterJob::fromJson({}).has_value());
        QJsonObject wrongSchema = job.toJson();
        wrongSchema.insert(u"schema_version"_s, 9);
        QVERIFY(!UpdaterJob::fromJson(wrongSchema).has_value());
        QJsonObject absurdTimeout = job.toJson();
        absurdTimeout.insert(u"wait_timeout_ms"_s, 999'999'999);
        QVERIFY(!UpdaterJob::fromJson(absurdTimeout).has_value());

        UpdateReport report;
        report.status = UpdateReport::Status::RolledBack;
        report.detail = u"x"_s;
        report.finishedAt = QDateTime::fromString(u"2026-11-10T12:00:00Z"_s, Qt::ISODate);
        const auto back = UpdateReport::fromJson(report.toJson());
        QVERIFY(back.has_value());
        QCOMPARE(back->status, UpdateReport::Status::RolledBack);
        QCOMPARE(back->finishedAt, report.finishedAt);
        QVERIFY(!UpdateReport::fromJson(QJsonObject {{u"status"_s, u"nonsense"_s}}).has_value());
    }
};

QTEST_GUILESS_MAIN(UpdaterTests)
#include "UpdaterTests.moc"
