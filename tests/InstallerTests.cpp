// Installation, update with backup and rollback, uninstallation, the list of
// installed projects, and the safeguards around the library folder.

#include "ZipBuilder.h"
#include "installer/FileOps.h"
#include "installer/InstalledProjects.h"
#include "installer/Installer.h"
#include "installer/LibraryLayout.h"
#include "installer/ProjectLauncher.h"
#include "installer/Uninstaller.h"
#include "security/HashVerifier.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <thread>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace Qt::StringLiterals;
using launcher::InstalledProject;
using launcher::InstalledProjects;
using launcher::Installer;
using launcher::InstallError;
using launcher::InstallRequest;
using launcher::InstallResult;
using launcher::LibraryLayout;
using launcher::Project;
using launcher::StartError;
using launcher::Uninstaller;
using launcher::UninstallError;
using launcher::UninstallResult;
namespace fileops = launcher::FileOps;
namespace validator = launcher::ArchiveValidator;

namespace {

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

/// A directory junction needs no special rights on Windows.
bool makeJunction(const QString& link, const QString& target)
{
    QProcess process;
    process.start(u"cmd.exe"_s,
        {u"/c"_s, u"mklink"_s, u"/J"_s, QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)});
    return process.waitForFinished(10000) && process.exitCode() == 0;
}

#ifdef Q_OS_WIN
/// Holds a file so that nobody else may even read it (a program that keeps its settings open exclusively).
class ExclusiveLock {
public:
    explicit ExclusiveLock(const QString& path)
        : m_handle(CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()),
              GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
    {
    }
    ~ExclusiveLock()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
    }
    bool isLocked() const { return m_handle != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_handle;
};

/// Keeps a file open the way a running program does: nobody may delete or rename it.
class FileLock {
public:
    explicit FileLock(const QString& path)
        : m_handle(CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()),
              GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr))
    {
    }
    ~FileLock() { release(); }
    bool isLocked() const { return m_handle != INVALID_HANDLE_VALUE; }
    void release()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE m_handle;
};
#endif

QDateTime utc(const QString& iso)
{
    return QDateTime::fromString(iso, Qt::ISODate).toUTC();
}

QStringList listFiles(const QString& root)
{
    QStringList files;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        files << QDir(root).relativeFilePath(it.next());
    files.sort();
    return files;
}

} // namespace

class InstallerTests : public QObject {
    Q_OBJECT

private:
    // Fresh portable folder for every test.
    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<LibraryLayout> m_layout;
    std::unique_ptr<InstalledProjects> m_installed;
    std::unique_ptr<Installer> m_installer;
    std::unique_ptr<Uninstaller> m_uninstaller;
    Project m_project;

    QString root(const QString& relative = QString()) const { return m_root->filePath(relative); }
    QString library(const QString& relative = QString()) const { return root(u"library/"_s + relative); }
    QString temp(const QString& relative = QString()) const { return root(u"temp/"_s + relative); }
    QString current(const QString& relative = QString()) const
    {
        return library(u"TestTool/current/"_s + relative);
    }
    QString backups(const QString& relative = QString()) const
    {
        return library(u"TestTool/backups/"_s + relative);
    }
    QString stateFile() const { return root(u"launcher/launcher.json"_s); }

    static QList<testzip::Entry> releaseFiles(const QString& version)
    {
        return {
            testzip::folder(u"bin/"_s),
            testzip::file(u"bin/Tool.exe"_s, "EXE-" + version.toUtf8()),
            testzip::file(u"data/version.txt"_s, version.toUtf8(), false),
            testzip::file(u"readme.txt"_s, "readme of " + version.toUtf8()),
        };
    }

    /// Writes an archive into downloads/ and returns the request for it.
    InstallRequest makeRequest(
        const QList<testzip::Entry>& entries, const QString& version, const QString& name = QString())
    {
        InstallRequest request;
        request.project = m_project;
        request.version = version;
        request.tag = u"v"_s + version;
        request.archivePath =
            root(u"downloads/test-tool/"_s + (name.isEmpty() ? u"Tool-"_s + version + u".zip"_s : name));
        [&] { QVERIFY(writeFile(request.archivePath, testzip::build(entries))); }();
        request.sha256 = launcher::HashVerifier::sha256File(request.archivePath);
        return request;
    }

    InstallRequest releaseRequest(const QString& version)
    {
        return makeRequest(releaseFiles(version), version);
    }

    /// Runs an installation to its end.
    InstallResult installAndWait(const InstallRequest& request)
    {
        QSignalSpy done(m_installer.get(), &Installer::installed);
        QSignalSpy failed(m_installer.get(), &Installer::failed);
        m_installer->install(request);
        InstallResult result;
        for (int i = 0; i < 200 && done.isEmpty() && failed.isEmpty(); ++i)
            QTest::qWait(50);
        if (!done.isEmpty())
            result = done.first().at(1).value<InstallResult>();
        else if (!failed.isEmpty())
            result = failed.first().at(1).value<InstallResult>();
        else {
            result.error = InstallError::RollbackFailed; // nothing arrived: fail loudly
            result.detail = u"TIMEOUT: neither installed() nor failed() was emitted"_s;
        }
        if (done.size() + failed.size() != 1) {
            result.error = InstallError::RollbackFailed;
            result.detail += u" [expected exactly one signal]"_s;
        }
        return result;
    }

    void expectInstalled(const QString& version)
    {
        QCOMPARE(readFile(current(u"bin/Tool.exe"_s)), "EXE-" + version.toUtf8());
        QCOMPARE(readFile(current(u"data/version.txt"_s)), version.toUtf8());
        const auto record = m_installed->get(u"test-tool"_s);
        QVERIFY(record.has_value());
        QCOMPARE(record->version, version);
    }

    QStringList leftovers() const { return QDir(temp()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); }

private slots:
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        QVERIFY(QDir().mkpath(library()));
        QVERIFY(QDir().mkpath(temp()));

        m_layout = std::make_unique<LibraryLayout>(library(), temp());
        m_installed = std::make_unique<InstalledProjects>(stateFile());
        m_installed->load();
        m_installer = std::make_unique<Installer>(*m_layout, *m_installed);
        m_uninstaller = std::make_unique<Uninstaller>(*m_layout, *m_installed);

        m_project = {};
        m_project.id = u"test-tool"_s;
        m_project.name = u"Test Tool"_s;
        m_project.owner = u"owner"_s;
        m_project.repository = u"repo"_s;
        m_project.assetPattern = u"Tool-*.zip"_s;
        m_project.executable = u"bin/Tool.exe"_s;
        m_project.installDirectory = u"TestTool"_s;
    }

    void cleanup()
    {
        m_uninstaller.reset();
        m_installer.reset();
        m_installed.reset();
        m_layout.reset();
        m_root.reset();
    }

    // === installation =========================================================

    void install_fresh()
    {
        QSignalSpy progress(m_installer.get(), &Installer::progress);
        const InstallResult result = installAndWait(releaseRequest(u"1.0.0"_s));
        QVERIFY2(result.ok(), qPrintable(result.detail));

        expectInstalled(u"1.0.0"_s);
        QCOMPARE(readFile(current(u"readme.txt"_s)), QByteArray("readme of 1.0.0"));
        QVERIFY(QFileInfo(current(u"bin"_s)).isDir());
        QVERIFY(result.backupDirectory.isEmpty());
        QVERIFY(!QFileInfo::exists(library(u"TestTool/backups"_s)));
        QVERIFY2(leftovers().isEmpty(), qPrintable(leftovers().join(u','))); // no staging folder remains

        // Both stages reported, the last one complete.
        QVERIFY(progress.size() >= 2);
        QCOMPARE(progress.first().at(1).value<Installer::Stage>(), Installer::Stage::Verifying);
        bool extracted = false;
        for (const QList<QVariant>& args : std::as_const(progress)) {
            if (args.at(1).value<Installer::Stage>() == Installer::Stage::Extracting
                && args.at(2).toLongLong() == args.at(3).toLongLong())
                extracted = true;
        }
        QVERIFY(extracted);

        // The record is on disk, with names only.
        InstalledProjects reloaded(stateFile());
        QCOMPARE(reloaded.load(), InstalledProjects::LoadResult::Loaded);
        const auto record = reloaded.get(u"test-tool"_s);
        QVERIFY(record.has_value());
        QCOMPARE(record->version, u"1.0.0"_s);
        QCOMPARE(record->tag, u"v1.0.0"_s);
        QCOMPARE(record->installDirectory, u"TestTool"_s);
        QCOMPARE(record->executable, u"bin/Tool.exe"_s);
        QVERIFY(record->installedAt.isValid());
        const QByteArray json = readFile(stateFile());
        QVERIFY(!json.contains(m_root->path().toUtf8()));
        QVERIFY(!json.contains("\\\\"));
    }

    void install_updateKeepsBackup()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        const QDateTime started = utc(u"2026-01-02T03:04:05Z"_s);
        m_installed->recordStart(u"test-tool"_s, started);
        QVERIFY(writeFile(current(u"user-data.cfg"_s),
            "created by the program")); // lands in the backup with the old version

        const InstallResult result = installAndWait(releaseRequest(u"2.0.0"_s));
        QVERIFY2(result.ok(), qPrintable(result.detail));
        expectInstalled(u"2.0.0"_s);
        QVERIFY(!QFileInfo::exists(current(u"user-data.cfg"_s)));

        QCOMPARE(QDir::cleanPath(result.backupDirectory), QDir::cleanPath(backups(u"1.0.0"_s)));
        QCOMPARE(readFile(backups(u"1.0.0/bin/Tool.exe"_s)), QByteArray("EXE-1.0.0"));
        QCOMPARE(readFile(backups(u"1.0.0/user-data.cfg"_s)), QByteArray("created by the program"));
        QCOMPARE(m_installed->get(u"test-tool"_s)->lastStartedAt, started);
        QVERIFY(leftovers().isEmpty());
    }

    void install_prunesOldBackups()
    {
        for (const QString& version : {u"1.0.0"_s, u"1.1.0"_s, u"1.2.0"_s, u"1.3.0"_s}) {
            QVERIFY(installAndWait(releaseRequest(version)).ok());
            QTest::qWait(30);
        }
        expectInstalled(u"1.3.0"_s);
        const QStringList kept = QDir(backups()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        QCOMPARE(kept, (QStringList {u"1.1.0"_s, u"1.2.0"_s}));
    }

    void install_sameVersionAgain()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        InstallResult third = installAndWait(releaseRequest(u"1.0.0"_s));
        QVERIFY(third.ok());
        expectInstalled(u"1.0.0"_s);

        // The earlier copies were kept under different names, none overwritten.
        const QStringList kept = QDir(backups()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(kept.size(), 2);
        QVERIFY(kept.contains(u"1.0.0"_s));
    }

    /// A second project, with files and a folder of its own.
    InstallRequest otherRequest(const QString& version)
    {
        InstallRequest request;
        request.project = m_project;
        request.project.id = u"other-tool"_s;
        request.project.name = u"Other Tool"_s;
        request.project.executable = u"Other.exe"_s;
        request.project.installDirectory = u"OtherTool"_s;
        request.version = version;
        request.tag = u"v"_s + version;
        request.archivePath = root(u"downloads/other-tool/Other-"_s + version + u".zip"_s);
        const QList<testzip::Entry> entries {
            testzip::file(u"Other.exe"_s, "OTHER-" + version.toUtf8()),
            testzip::file(u"readme.txt"_s, "readme of the other tool"),
        };
        [&] { QVERIFY(writeFile(request.archivePath, testzip::build(entries))); }();
        request.sha256 = launcher::HashVerifier::sha256File(request.archivePath);
        return request;
    }

    void install_everyProjectGetsAFolderOfItsOwn()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        const InstallResult second = installAndWait(otherRequest(u"5.0.0"_s));
        QVERIFY2(second.ok(), qPrintable(second.detail));

        // Two folders in the library, each with its own files and nothing of the other's.
        QCOMPARE(QDir(library()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name),
            (QStringList {u"OtherTool"_s, u"TestTool"_s}));
        QCOMPARE(readFile(current(u"bin/Tool.exe"_s)), QByteArray("EXE-1.0.0"));
        QVERIFY(!QFileInfo::exists(current(u"Other.exe"_s)));
        QCOMPARE(readFile(library(u"OtherTool/current/Other.exe"_s)), QByteArray("OTHER-5.0.0"));
        QVERIFY(!QFileInfo::exists(library(u"OtherTool/current/bin"_s)));

        // The list keeps them apart, too.
        QCOMPARE(m_installed->get(u"test-tool"_s)->installDirectory, u"TestTool"_s);
        QCOMPARE(m_installed->get(u"other-tool"_s)->installDirectory, u"OtherTool"_s);

        // Updating one leaves the other exactly as it was, backups included.
        const QByteArray otherBefore = readFile(library(u"OtherTool/current/Other.exe"_s));
        QVERIFY(installAndWait(releaseRequest(u"2.0.0"_s)).ok());
        expectInstalled(u"2.0.0"_s);
        QCOMPARE(readFile(library(u"OtherTool/current/Other.exe"_s)), otherBefore);
        QCOMPARE(m_installed->get(u"other-tool"_s)->version, u"5.0.0"_s);
        QVERIFY(!QFileInfo::exists(library(u"OtherTool/backups"_s)));
        QVERIFY(QFileInfo::exists(backups(u"1.0.0/bin/Tool.exe"_s)));
    }

    void install_refusesTheFolderOfAnotherInstalledProject()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());

        // Another project that asks for the same folder (in other letters, as Windows does not tell them
        // apart).
        InstallRequest intruder = otherRequest(u"5.0.0"_s);
        intruder.project.installDirectory = u"testtool"_s;
        const InstallResult result = installAndWait(intruder);
        QCOMPARE(result.error, InstallError::InvalidProject);
        QVERIFY(result.detail.contains(u"test-tool"_s));

        // The installed project is untouched, and the intruder was not registered.
        expectInstalled(u"1.0.0"_s);
        QVERIFY(!QFileInfo::exists(backups()));
        QVERIFY(!m_installed->contains(u"other-tool"_s));
        QVERIFY(leftovers().isEmpty());

        // The same project may of course be installed again (a reinstall or an update).
        QVERIFY(installAndWait(releaseRequest(u"1.1.0"_s)).ok());
    }

    // --- the project's own data survives an update (D-47) ----------------------------------

    /// Writes data the way a running project would: settings and a cache beside the program, and a scratch
    /// file.
    void writeUserData()
    {
        QVERIFY(writeFile(current(u"settings/profile.json"_s), "mine"));
        QVERIFY(writeFile(current(u"settings/deep/cache.bin"_s), "cache"));
        QVERIFY(writeFile(current(u"user.cfg"_s), "cfg"));
        QVERIFY(writeFile(current(u"scratch.tmp"_s), "scratch"));
    }

    void update_carriesTheProjectsOwnData()
    {
        m_project.keep = {u"settings"_s, u"user.cfg"_s};
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        writeUserData();

        const InstallResult result = installAndWait(releaseRequest(u"2.0.0"_s));
        QVERIFY2(result.ok(), qPrintable(result.detail));
        expectInstalled(u"2.0.0"_s); // the new program is there ...

        // ... with the data the user made, folders and files, deep inside too.
        QCOMPARE(readFile(current(u"settings/profile.json"_s)), QByteArray("mine"));
        QCOMPARE(readFile(current(u"settings/deep/cache.bin"_s)), QByteArray("cache"));
        QCOMPARE(readFile(current(u"user.cfg"_s)), QByteArray("cfg"));
        // What is not named stays with the old version, as before.
        QVERIFY(!QFileInfo::exists(current(u"scratch.tmp"_s)));
        QCOMPARE(readFile(backups(u"1.0.0/scratch.tmp"_s)), QByteArray("scratch"));
        // And the old version keeps its own copy: nothing is moved away from it.
        QCOMPARE(readFile(backups(u"1.0.0/settings/profile.json"_s)), QByteArray("mine"));
        QCOMPARE(readFile(backups(u"1.0.0/user.cfg"_s)), QByteArray("cfg"));
        QVERIFY(leftovers().isEmpty());

        // A third version brings the data along again.
        QVERIFY(installAndWait(releaseRequest(u"3.0.0"_s)).ok());
        QCOMPARE(readFile(current(u"settings/profile.json"_s)), QByteArray("mine"));
    }

    void update_theUsersFileWinsAndNewFilesStay()
    {
        // "data" is kept, and the new version also has files in it.
        m_project.keep = {u"data"_s};
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok()); // has data/version.txt
        QVERIFY(writeFile(current(u"data/version.txt"_s), "edited by the user"));
        QVERIFY(writeFile(current(u"data/mine.txt"_s), "mine"));

        QList<testzip::Entry> entries = releaseFiles(u"2.0.0"_s);
        entries.append(testzip::file(u"data/new-default.json"_s, "a default of 2.0.0"));
        QVERIFY(installAndWait(makeRequest(entries, u"2.0.0"_s)).ok());

        QCOMPARE(readFile(current(u"bin/Tool.exe"_s)), QByteArray("EXE-2.0.0"));
        QCOMPARE(
            readFile(current(u"data/version.txt"_s)), QByteArray("edited by the user")); // the user's wins
        QCOMPARE(readFile(current(u"data/mine.txt"_s)), QByteArray("mine"));
        QCOMPARE(
            readFile(current(u"data/new-default.json"_s)), QByteArray("a default of 2.0.0")); // the new stays
    }

    void update_aKeptItemThatIsNotThereIsNoProblem()
    {
        m_project.keep = {u"settings"_s, u"user.cfg"_s};
        // The first installation has nothing to carry over, and the second version finds nothing to carry.
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        QVERIFY(installAndWait(releaseRequest(u"2.0.0"_s)).ok());
        expectInstalled(u"2.0.0"_s);
        QVERIFY(!QFileInfo::exists(current(u"settings"_s)));
        QVERIFY(!QFileInfo::exists(current(u"user.cfg"_s)));
    }

    void update_aSingleProgramKeepsItsDataToo()
    {
        // As KERS Overlay: the program alone is the release, and it writes "data" next to itself.
        useSingleFileProject();
        m_project.keep = {u"data"_s};
        QVERIFY(installAndWait(programRequest(u"1.0.0"_s)).ok());
        QVERIFY(writeFile(current(u"data/settings.json"_s), "{\"overlay\": true}"));

        QVERIFY(installAndWait(programRequest(u"2.0.0"_s)).ok());
        QVERIFY(readFile(current(u"Tool.exe"_s)).endsWith("PROGRAM-2.0.0"));
        QCOMPARE(readFile(current(u"data/settings.json"_s)), QByteArray("{\"overlay\": true}"));
        QCOMPARE(m_installed->get(u"test-tool"_s)->version, u"2.0.0"_s);
    }

#ifdef Q_OS_WIN
    void update_dataThatCannotBeCopiedStopsTheUpdate()
    {
        m_project.keep = {u"settings"_s};
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        writeUserData();

        {
            // The program holds its settings exclusively: they cannot be read, so they cannot be carried
            // over.
            ExclusiveLock lock(current(u"settings/profile.json"_s));
            QVERIFY(lock.isLocked());
            const InstallResult result = installAndWait(releaseRequest(u"2.0.0"_s));
            QCOMPARE(result.error, InstallError::CarryOverFailed);
            QVERIFY(result.detail.contains(u"settings"_s));

            // Nothing happened: the old version, its data and its record are as they were; no backup, no
            // staging.
            expectInstalled(u"1.0.0"_s);
            QVERIFY(QFileInfo::exists(
                current(u"settings/profile.json"_s))); // (it cannot be read while it is held)
            QVERIFY(!QFileInfo::exists(backups()));
            QVERIFY(leftovers().isEmpty());
        }
        QCOMPARE(readFile(current(u"settings/profile.json"_s)), QByteArray("mine"));

        // Without the lock, the same update works.
        QVERIFY(installAndWait(releaseRequest(u"2.0.0"_s)).ok());
        QCOMPARE(readFile(current(u"settings/profile.json"_s)), QByteArray("mine"));
    }

    void update_aLinkInTheDataIsNeverFollowed()
    {
        m_project.keep = {u"data"_s};
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        QVERIFY(writeFile(outside.filePath(u"precious.txt"_s), "outside"));
        QVERIFY(writeFile(current(u"data/real.txt"_s), "real"));
        if (!makeJunction(current(u"data/linked"_s), outside.path()))
            QSKIP("directory junctions cannot be created here");

        QVERIFY(installAndWait(releaseRequest(u"2.0.0"_s)).ok());
        QCOMPARE(readFile(current(u"data/real.txt"_s)), QByteArray("real"));
        QVERIFY(!QFileInfo::exists(current(u"data/linked"_s))); // the link was left out, not copied
        QCOMPARE(readFile(outside.filePath(u"precious.txt"_s)),
            QByteArray("outside")); // and nothing was reached through it
    }

    void update_aKeptFolderThatIsALinkIsSkipped()
    {
        m_project.keep = {u"data"_s};
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        QVERIFY(writeFile(outside.filePath(u"secret.txt"_s), "secret"));
        QVERIFY(QDir(current()).removeRecursively() && QDir().mkpath(current()));
        QVERIFY(writeFile(current(u"bin/Tool.exe"_s), "EXE-1.0.0"));
        if (!makeJunction(current(u"data"_s), outside.path()))
            QSKIP("directory junctions cannot be created here");

        QVERIFY(installAndWait(releaseRequest(u"2.0.0"_s)).ok());
        // "data" of the new version is the one from the package; nothing from behind the link came along.
        QVERIFY(!QFileInfo::exists(current(u"data/secret.txt"_s)));
        QCOMPARE(readFile(outside.filePath(u"secret.txt"_s)), QByteArray("secret"));
    }
#endif

    void install_refusesKeptDataThatContainsTheProgram()
    {
        m_project.keep = {u"bin"_s}; // the start file is bin/Tool.exe: the old one would replace the new one
        const InstallResult result = installAndWait(releaseRequest(u"1.0.0"_s));
        QCOMPARE(result.error, InstallError::InvalidProject);
        QVERIFY(result.detail.contains(u"bin"_s));
        QVERIFY(!QFileInfo::exists(current()));
    }

    void install_firstInstallWithExistingUnregisteredFolder()
    {
        // A current/ folder without a record (the list was lost): kept as a backup, not deleted.
        QVERIFY(writeFile(current(u"bin/Tool.exe"_s), "OLD"));
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        expectInstalled(u"1.0.0"_s);
        QCOMPARE(readFile(backups(u"unknown/bin/Tool.exe"_s)), QByteArray("OLD"));
    }

    // --- a release that is a single program, not a ZIP -------------------------

    /// A project whose release file is the program itself, like KERS Overlay's.
    void useSingleFileProject()
    {
        m_project.assetPattern = u"Tool*.exe"_s;
        m_project.executable = u"Tool.exe"_s;
    }

    /// A downloaded program: "MZ" like every Windows program, then a body that tells the versions apart.
    InstallRequest programRequest(const QString& version, const QByteArray& content = {})
    {
        InstallRequest request;
        request.project = m_project;
        request.version = version;
        request.tag = u"Tool"_s + version;
        request.archivePath = root(u"downloads/test-tool/Tool_"_s + version + u".exe"_s);
        const QByteArray bytes = content.isNull()
            ? QByteArray("MZ") + QByteArray(100, 'x') + "PROGRAM-" + version.toUtf8()
            : content;
        [&] { QVERIFY(writeFile(request.archivePath, bytes)); }();
        request.sha256 = launcher::HashVerifier::sha256File(request.archivePath);
        return request;
    }

    void install_singleFile_fresh()
    {
        useSingleFileProject();
        QSignalSpy progress(m_installer.get(), &Installer::progress);
        const InstallResult result = installAndWait(programRequest(u"1.0.0"_s));
        QVERIFY2(result.ok(), qPrintable(result.detail));

        // The program, under the name from the project entry, and nothing else in the folder.
        QVERIFY(readFile(current(u"Tool.exe"_s)).endsWith("PROGRAM-1.0.0"));
        QCOMPARE(
            QDir(current()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), QStringList {u"Tool.exe"_s});
        const auto record = m_installed->get(u"test-tool"_s);
        QVERIFY(record.has_value());
        QCOMPARE(record->version, u"1.0.0"_s);
        QCOMPARE(record->executable, u"Tool.exe"_s);
        QVERIFY2(leftovers().isEmpty(), qPrintable(leftovers().join(u','))); // no staging folder remains
        QVERIFY(progress.size() >= 2);
    }

    void install_singleFile_updateKeepsBackupAndRollsBack()
    {
        useSingleFileProject();
        QVERIFY(installAndWait(programRequest(u"1.0.0"_s)).ok());
        const InstallResult updated = installAndWait(programRequest(u"2.0.0"_s));
        QVERIFY2(updated.ok(), qPrintable(updated.detail));
        QVERIFY(readFile(current(u"Tool.exe"_s)).endsWith("PROGRAM-2.0.0"));
        QVERIFY(readFile(backups(u"1.0.0/Tool.exe"_s)).endsWith("PROGRAM-1.0.0"));
        QCOMPARE(m_installed->get(u"test-tool"_s)->version, u"2.0.0"_s);

        // A third version that fails on the way leaves 2.0.0 as it was.
        const InstallResult bad = installAndWait(programRequest(u"3.0.0"_s, "this is not a program"));
        QCOMPARE(bad.error, InstallError::NotAProgram);
        QVERIFY(readFile(current(u"Tool.exe"_s)).endsWith("PROGRAM-2.0.0"));
        QCOMPARE(m_installed->get(u"test-tool"_s)->version, u"2.0.0"_s);
        QVERIFY(leftovers().isEmpty());
    }

    void install_singleFile_mustBeAWindowsProgram_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("a ZIP archive") << (QByteArray("PK\x03\x04") + QByteArray(200, 'z'));
        QTest::newRow("a script") << (QByteArray("#!/bin/sh\n") + QByteArray(200, ' '));
        QTest::newRow("text") << QByteArray(300, 'a');
        QTest::newRow("only the signature") << QByteArray("MZ");
        QTest::newRow("too small to be a program") << (QByteArray("MZ") + QByteArray(40, 'x'));
        QTest::newRow("signature in the wrong case") << (QByteArray("mz") + QByteArray(200, 'x'));
    }

    void install_singleFile_mustBeAWindowsProgram()
    {
        QFETCH(QByteArray, content);
        useSingleFileProject();
        // The hash is right (it is the hash of exactly this file); the file still is no program.
        const InstallResult result = installAndWait(programRequest(u"1.0.0"_s, content));
        QCOMPARE(result.error, InstallError::NotAProgram);
        QVERIFY(!QFileInfo::exists(current(u"Tool.exe"_s)));
        QVERIFY(!m_installed->get(u"test-tool"_s).has_value());
        QVERIFY(leftovers().isEmpty());
    }

    void install_singleFile_stillNeedsTheRightHash()
    {
        useSingleFileProject();
        InstallRequest request = programRequest(u"1.0.0"_s);
        request.sha256 = QString(64, u'0');
        QCOMPARE(installAndWait(request).error, InstallError::HashMismatch);
        request.sha256.clear();
        QCOMPARE(installAndWait(request).error, InstallError::NoTrustedHash);
        QVERIFY(!QFileInfo::exists(current(u"Tool.exe"_s)));
    }

    void install_singleFile_executableIsAFileNameNotAPath()
    {
        useSingleFileProject();
        m_project.executable = u"bin/Tool.exe"_s; // a path makes no sense for a single file
        QCOMPARE(installAndWait(programRequest(u"1.0.0"_s)).error, InstallError::InvalidProject);
        QVERIFY(!QFileInfo::exists(current(u"bin"_s)));
    }

    // --- integrity ----------------------------------------------------------

    void install_hashProblems()
    {
        InstallRequest request = releaseRequest(u"1.0.0"_s);

        InstallRequest wrong = request;
        wrong.sha256 = u"0"_s.repeated(64);
        InstallResult result = installAndWait(wrong);
        QCOMPARE(result.error, InstallError::HashMismatch);
        QVERIFY(result.detail.contains(request.sha256));

        InstallRequest none = request;
        none.sha256.clear();
        QCOMPARE(installAndWait(none).error, InstallError::NoTrustedHash);

        InstallRequest garbage = request;
        garbage.sha256 = u"sha256:"_s + request.sha256;
        QCOMPARE(installAndWait(garbage).error, InstallError::NoTrustedHash);

        InstallRequest uppercase = request;
        uppercase.sha256 = request.sha256.toUpper();
        QVERIFY(installAndWait(uppercase).ok()); // case does not matter

        // Nothing of the failures reached the library.
        QVERIFY(m_installed->contains(u"test-tool"_s));
    }

    void install_hashProblem_leavesLibraryUntouched()
    {
        InstallRequest request = releaseRequest(u"1.0.0"_s);
        request.sha256 = u"f"_s.repeated(64);
        QCOMPARE(installAndWait(request).error, InstallError::HashMismatch);
        QVERIFY(!QFileInfo::exists(library(u"TestTool"_s)));
        QVERIFY(!m_installed->contains(u"test-tool"_s));
        QVERIFY(leftovers().isEmpty());
    }

    void install_tamperedAfterDownload()
    {
        // The archive changes between the hash check of the release and the
        // installation: the installer verifies the file it extracts.
        InstallRequest request = releaseRequest(u"1.0.0"_s);
        QVERIFY(writeFile(request.archivePath, testzip::build(releaseFiles(u"6.6.6"_s))));
        QCOMPARE(installAndWait(request).error, InstallError::HashMismatch);
        QVERIFY(!m_installed->contains(u"test-tool"_s));
    }

    // --- hostile and damaged archives -----------------------------------------

    void install_rejectedArchives_data()
    {
        QTest::addColumn<QList<testzip::Entry>>("entries");
        QTest::addColumn<int>("problem");

        using P = validator::Problem;
        const auto good = [] { return releaseFiles(u"9.9.9"_s); };
        const auto plus = [&](const testzip::Entry& entry) {
            QList<testzip::Entry> entries = good();
            entries.append(entry);
            return entries;
        };
        const auto file = [](const QString& name) { return testzip::file(name, "evil"); };

        QTest::newRow("zip slip") << plus(file(u"../escaped.txt"_s)) << int(P::UnsafePath);
        QTest::newRow("zip slip deep") << plus(file(u"a/b/../../../escaped.txt"_s)) << int(P::UnsafePath);
        QTest::newRow("zip slip backslash") << plus(file(u"..\\escaped.txt"_s)) << int(P::UnsafePath);
        QTest::newRow("absolute") << plus(file(u"/escaped.txt"_s)) << int(P::UnsafePath);
        QTest::newRow("drive") << plus(file(u"C:/escaped.txt"_s)) << int(P::UnsafePath);
        QTest::newRow("unc") << plus(file(u"\\\\host\\share\\escaped.txt"_s)) << int(P::UnsafePath);
        QTest::newRow("stream") << plus(file(u"readme.txt:evil"_s)) << int(P::UnsafePath);
        QTest::newRow("reserved") << plus(file(u"aux.txt"_s)) << int(P::UnsafePath);
        QTest::newRow("duplicate") << plus(file(u"BIN/TOOL.EXE"_s)) << int(P::DuplicatePath);
        QTest::newRow("file and folder") << plus(file(u"readme.txt/inner"_s)) << int(P::ConflictingPath);

        testzip::Entry link = file(u"link"_s);
        link.unixMode = 0120777;
        QTest::newRow("symlink") << plus(link) << int(P::UnsupportedEntry);

        QTest::newRow("no start file")
            << QList<testzip::Entry> {testzip::file(u"readme.txt"_s, "x")} << int(P::MissingExecutable);
        QTest::newRow("start file in a subfolder")
            << QList<testzip::Entry> {testzip::file(u"app/bin/Tool.exe"_s, "x")} << int(P::MissingExecutable);
        QTest::newRow("empty") << QList<testzip::Entry> {} << int(P::Empty);
    }

    void install_rejectedArchives()
    {
        QFETCH(QList<testzip::Entry>, entries);
        QFETCH(int, problem);

        // Once on an empty library and once on top of a working installation.
        for (const bool existing : {false, true}) {
            if (existing) {
                QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
            }
            const InstallResult result = installAndWait(makeRequest(entries, u"9.9.9"_s, u"evil.zip"_s));
            QCOMPARE(result.error, InstallError::ArchiveRejected);
            QCOMPARE(int(result.validationProblem), problem);

            QVERIFY(!QFileInfo::exists(root(u"escaped.txt"_s)));
            QVERIFY(!QFileInfo::exists(library(u"escaped.txt"_s)));
            QVERIFY(!QFileInfo::exists(temp(u"escaped.txt"_s)));
            QVERIFY(leftovers().isEmpty());
            if (existing) {
                expectInstalled(u"1.0.0"_s);
                QVERIFY(!QFileInfo::exists(backups()));
            } else {
                QVERIFY(!QFileInfo::exists(library(u"TestTool"_s)));
                QVERIFY(!m_installed->contains(u"test-tool"_s));
            }
        }
    }

    void install_notAnArchive()
    {
        InstallRequest request = releaseRequest(u"1.0.0"_s);
        QVERIFY(writeFile(request.archivePath, "this is not a zip file"));
        request.sha256 = launcher::HashVerifier::sha256File(request.archivePath);
        QCOMPARE(installAndWait(request).error, InstallError::ArchiveUnreadable);

        request.archivePath = root(u"downloads/missing.zip"_s);
        QCOMPARE(installAndWait(request).error, InstallError::ArchiveUnreadable);
        QVERIFY(leftovers().isEmpty());
    }

    void install_damagedData_keepsInstalledVersion()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());

        QList<testzip::Entry> entries = releaseFiles(u"2.0.0"_s);
        entries[3].crc = 0xDEADBEEF; // the last file is damaged, after others were written
        const InstallResult result = installAndWait(makeRequest(entries, u"2.0.0"_s));
        QCOMPARE(result.error, InstallError::ExtractionFailed);

        expectInstalled(u"1.0.0"_s);
        QVERIFY(!QFileInfo::exists(backups()));
        QVERIFY2(leftovers().isEmpty(), qPrintable(leftovers().join(u',')));
    }

    void install_bombIsRejectedBeforeAnythingIsWritten()
    {
        validator::Limits limits;
        limits.maxTotalSize = 1000;
        m_installer->setArchiveLimits(limits);
        const InstallResult result = installAndWait(
            makeRequest({testzip::file(u"bin/Tool.exe"_s, QByteArray(5000, 'x'))}, u"1.0.0"_s));
        QCOMPARE(result.error, InstallError::ArchiveRejected);
        QCOMPARE(result.validationProblem, validator::Problem::TooLarge);
        QVERIFY(leftovers().isEmpty());
    }

    // --- protecting a running program --------------------------------------

    void install_projectRunning()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        m_installer->setRunningCheck([](const QString& id) { return id == u"test-tool"_s; });

        QCOMPARE(installAndWait(releaseRequest(u"2.0.0"_s)).error, InstallError::ProjectRunning);
        expectInstalled(u"1.0.0"_s);
        QVERIFY(leftovers().isEmpty());
    }

#ifdef Q_OS_WIN
    void install_lockedFile_isNotReplaced()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());

        FileLock lock(current(u"bin/Tool.exe"_s));
        QVERIFY(lock.isLocked());

        const InstallResult result = installAndWait(releaseRequest(u"2.0.0"_s));
        QCOMPARE(result.error, InstallError::ProjectRunning);
        expectInstalled(u"1.0.0"_s);
        QVERIFY(!QFileInfo::exists(backups(u"1.0.0"_s)));
        QVERIFY2(leftovers().isEmpty(), qPrintable(leftovers().join(u',')));

        // After the program ends, the update goes through.
        lock.release();
        QVERIFY(installAndWait(releaseRequest(u"2.0.0"_s)).ok());
        expectInstalled(u"2.0.0"_s);
    }
#endif

    // --- all or nothing -------------------------------------------------------

    void install_registrationFails_restoresPreviousVersion()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());

        // The state file can no longer be replaced: a folder sits in its place.
        QVERIFY(QFile::remove(stateFile()));
        QVERIFY(QDir().mkpath(stateFile()));

        const InstallResult result = installAndWait(releaseRequest(u"2.0.0"_s));
        QCOMPARE(result.error, InstallError::RegistrationFailed);

        QCOMPARE(readFile(current(u"bin/Tool.exe"_s)), QByteArray("EXE-1.0.0"));
        QCOMPARE(readFile(current(u"data/version.txt"_s)), QByteArray("1.0.0"));
        QCOMPARE(m_installed->get(u"test-tool"_s)->version, u"1.0.0"_s);
        QVERIFY(!QFileInfo::exists(backups(u"1.0.0"_s)));
        QVERIFY(leftovers().isEmpty());
    }

    void install_registrationFails_firstInstallLeavesNothing()
    {
        QVERIFY(QDir().mkpath(stateFile())); // a folder where the state file should be
        const InstallResult result = installAndWait(releaseRequest(u"1.0.0"_s));
        QCOMPARE(result.error, InstallError::RegistrationFailed);
        QVERIFY(!QFileInfo::exists(library(u"TestTool"_s)));
        QVERIFY(!m_installed->contains(u"test-tool"_s));
        QVERIFY(leftovers().isEmpty());
    }

    void install_staleStagingIsReplaced()
    {
        QVERIFY(writeFile(temp(u"staging-test-tool/stale.txt"_s), "left by a crash"));
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        QVERIFY(!QFileInfo::exists(current(u"stale.txt"_s)));
        QVERIFY(leftovers().isEmpty());
    }

    void install_busy()
    {
        QSignalSpy done(m_installer.get(), &Installer::installed);
        QSignalSpy failed(m_installer.get(), &Installer::failed);
        const InstallRequest request = releaseRequest(u"1.0.0"_s);
        m_installer->install(request);
        QVERIFY(m_installer->isBusy(u"test-tool"_s));
        m_installer->install(request);

        // The second request is refused at once; the first still completes.
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().at(1).value<InstallResult>().error, InstallError::Busy);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
        QVERIFY(!m_installer->isBusy(u"test-tool"_s));
        expectInstalled(u"1.0.0"_s);
    }

    void install_insufficientSpace()
    {
        m_installer->setAvailableSpaceFunction([](const QString&) { return qint64(1000); });
        QCOMPARE(installAndWait(releaseRequest(u"1.0.0"_s)).error, InstallError::InsufficientSpace);
        QVERIFY(leftovers().isEmpty());
        QVERIFY(!QFileInfo::exists(library(u"TestTool"_s)));

        m_installer->setAvailableSpaceFunction(
            [](const QString&) { return qint64(-1); }); // unknown: not checked
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
    }

    void install_invalidProjects_data()
    {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QString>("value");

        QTest::newRow("folder parent") << u"installDirectory"_s << u".."_s;
        QTest::newRow("folder path") << u"installDirectory"_s << u"a/b"_s;
        QTest::newRow("folder drive") << u"installDirectory"_s << u"C:"_s;
        QTest::newRow("folder reserved") << u"installDirectory"_s << u"CON"_s;
        QTest::newRow("folder empty") << u"installDirectory"_s << QString();
        QTest::newRow("exe parent") << u"executable"_s << u"../Tool.exe"_s;
        QTest::newRow("exe absolute") << u"executable"_s << u"C:/Windows/notepad.exe"_s;
        QTest::newRow("exe not exe") << u"executable"_s << u"bin/Tool.bat"_s;
        QTest::newRow("id traversal") << u"id"_s << u"../x"_s;
        QTest::newRow("id uppercase") << u"id"_s << u"Test-Tool"_s;
        QTest::newRow("version") << u"version"_s << u"latest"_s;
        QTest::newRow("version path") << u"version"_s << u"1.0.0/../.."_s;
    }

    void install_invalidProjects()
    {
        QFETCH(QString, field);
        QFETCH(QString, value);

        InstallRequest request = releaseRequest(u"1.0.0"_s);
        if (field == u"installDirectory"_s)
            request.project.installDirectory = value;
        else if (field == u"executable"_s)
            request.project.executable = value;
        else if (field == u"id"_s)
            request.project.id = value;
        else
            request.version = value;

        QCOMPARE(installAndWait(request).error, InstallError::InvalidProject);
        QVERIFY(QDir(library()).isEmpty());
        QVERIFY(leftovers().isEmpty());
    }

    void install_projectFolderIsALink()
    {
        QTemporaryDir outside;
        QVERIFY(writeFile(outside.filePath(u"sentinel.txt"_s), "keep"));
        if (!makeJunction(library(u"TestTool"_s), outside.path()))
            QSKIP("cannot create a junction here");

        const InstallResult result = installAndWait(releaseRequest(u"1.0.0"_s));
        QCOMPARE(result.error, InstallError::SwapFailed);
        QCOMPARE(readFile(outside.filePath(u"sentinel.txt"_s)), QByteArray("keep"));
        QCOMPARE(listFiles(outside.path()), QStringList {u"sentinel.txt"_s});
        QVERIFY(!m_installed->contains(u"test-tool"_s));
        QVERIFY(leftovers().isEmpty());
        fileops::removeTree(library(u"TestTool"_s)); // removes the junction only
    }

    void install_pathTooLong()
    {
        // A launcher folder buried so deep that no room is left for the project's files.
        const QString deep = m_root->filePath(u"d"_s.repeated(100) + u"/"_s + u"e"_s.repeated(100));
        if (!QDir().mkpath(deep + u"/library"_s) || !QDir().mkpath(deep + u"/temp"_s))
            QSKIP("cannot create a path this long here");

        LibraryLayout layout(deep + u"/library"_s, deep + u"/temp"_s);
        Installer installer(layout, *m_installed);
        QSignalSpy failed(&installer, &Installer::failed);
        installer.install(releaseRequest(u"1.0.0"_s));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 20000);
        QCOMPARE(failed.first().at(1).value<InstallResult>().error, InstallError::PathTooLong);
    }

    void install_destructorWaitsForTheWorker()
    {
        m_installer->install(releaseRequest(u"1.0.0"_s));
        m_installer.reset(); // must neither crash nor leave a half-written library

        // Either the installation completed or it left nothing behind.
        QVERIFY(!QFileInfo::exists(temp(u"staging-test-tool"_s)));
        QCOMPARE(QFileInfo::exists(current(u"bin/Tool.exe"_s)), m_installed->contains(u"test-tool"_s));
    }

    void install_cancelOfUnknownProjectDoesNothing()
    {
        m_installer->cancel(u"nobody"_s);
        QVERIFY(!m_installer->isBusy(u"nobody"_s));
    }

    // === uninstallation =======================================================

    UninstallResult uninstallAndWait(const QString& id, bool* cleanupComplete = nullptr)
    {
        QSignalSpy cleaned(m_uninstaller.get(), &Uninstaller::cleanupFinished);
        const UninstallResult result = m_uninstaller->uninstall(id);
        if (result.ok() && QFileInfo::exists(m_layout->trashDir(id))) {
            cleaned.wait(20000);
            if (cleanupComplete != nullptr && !cleaned.isEmpty())
                *cleanupComplete = cleaned.first().at(1).toBool();
        }
        return result;
    }

    void uninstall_removesTheProject()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        QVERIFY(installAndWait(releaseRequest(u"2.0.0"_s)).ok()); // leaves a backup, which goes too
        QVERIFY(QFileInfo::exists(backups(u"1.0.0"_s)));

        QSignalSpy uninstalled(m_uninstaller.get(), &Uninstaller::uninstalled);
        bool complete = false;
        const UninstallResult result = uninstallAndWait(u"test-tool"_s, &complete);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QVERIFY(complete);
        QCOMPARE(uninstalled.size(), 1);

        QVERIFY(!QFileInfo::exists(library(u"TestTool"_s)));
        QVERIFY(!m_installed->contains(u"test-tool"_s));
        QVERIFY2(leftovers().isEmpty(), qPrintable(leftovers().join(u',')));
        // Persisted.
        InstalledProjects reloaded(stateFile());
        reloaded.load();
        QVERIFY(!reloaded.contains(u"test-tool"_s));

        // The project can be installed again.
        QVERIFY(installAndWait(releaseRequest(u"2.0.0"_s)).ok());
        expectInstalled(u"2.0.0"_s);
    }

    void uninstall_leavesOtherProjectsAlone()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        QVERIFY(writeFile(library(u"OtherTool/current/Other.exe"_s), "other"));
        QVERIFY(writeFile(root(u"downloads/test-tool/keep.zip"_s), "download"));
        QVERIFY(writeFile(root(u"launcher/settings.json"_s), "{}"));

        QVERIFY(uninstallAndWait(u"test-tool"_s).ok());
        QCOMPARE(readFile(library(u"OtherTool/current/Other.exe"_s)), QByteArray("other"));
        QVERIFY(QFileInfo::exists(root(u"downloads/test-tool/keep.zip"_s)));
        QVERIFY(QFileInfo::exists(root(u"launcher/settings.json"_s)));
        QVERIFY(QFileInfo::exists(library()));
    }

    void uninstall_notInstalled()
    {
        QVERIFY(writeFile(library(u"TestTool/current/bin/Tool.exe"_s), "unregistered"));
        QCOMPARE(m_uninstaller->uninstall(u"test-tool"_s).error, UninstallError::NotInstalled);
        QCOMPARE(m_uninstaller->uninstall(u"../x"_s).error, UninstallError::NotInstalled);
        QVERIFY(QFileInfo::exists(current(u"bin/Tool.exe"_s))); // not touched
    }

    void uninstall_running()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        m_uninstaller->setRunningCheck([](const QString&) { return true; });
        QCOMPARE(m_uninstaller->uninstall(u"test-tool"_s).error, UninstallError::ProjectRunning);
        expectInstalled(u"1.0.0"_s);
    }

#ifdef Q_OS_WIN
    void uninstall_lockedFile()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        FileLock lock(current(u"bin/Tool.exe"_s));
        QVERIFY(lock.isLocked());

        QCOMPARE(m_uninstaller->uninstall(u"test-tool"_s).error, UninstallError::ProjectRunning);
        expectInstalled(u"1.0.0"_s); // fully intact, record included
        QVERIFY(!QFileInfo::exists(m_layout->trashDir(u"test-tool"_s)));
        lock.release();
        QVERIFY(uninstallAndWait(u"test-tool"_s).ok());
    }
#endif

    void uninstall_folderAlreadyGone()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        QVERIFY(fileops::removeTree(library(u"TestTool"_s)));
        QVERIFY(m_uninstaller->uninstall(u"test-tool"_s).ok());
        QVERIFY(!m_installed->contains(u"test-tool"_s));
    }

    void uninstall_readOnlyFiles()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        QVERIFY(QFile::setPermissions(current(u"bin/Tool.exe"_s), QFileDevice::ReadOwner));
        QVERIFY(QFile::setPermissions(current(u"readme.txt"_s), QFileDevice::ReadOwner));

        bool complete = false;
        QVERIFY(uninstallAndWait(u"test-tool"_s, &complete).ok());
        QVERIFY(complete);
        QVERIFY(!QFileInfo::exists(library(u"TestTool"_s)));
        QVERIFY(leftovers().isEmpty());
    }

    void uninstall_doesNotFollowLinksInsideTheProject()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        QTemporaryDir outside;
        QVERIFY(writeFile(outside.filePath(u"sentinel.txt"_s), "keep"));
        if (!makeJunction(current(u"linked"_s), outside.path()))
            QSKIP("cannot create a junction here");

        bool complete = false;
        QVERIFY(uninstallAndWait(u"test-tool"_s, &complete).ok());
        QVERIFY(complete);
        QVERIFY(!QFileInfo::exists(library(u"TestTool"_s)));
        QCOMPARE(readFile(outside.filePath(u"sentinel.txt"_s)), QByteArray("keep"));
    }

    void uninstall_refusesAProjectFolderThatIsALink()
    {
        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        QTemporaryDir outside;
        QVERIFY(writeFile(outside.filePath(u"sentinel.txt"_s), "keep"));
        QVERIFY(QDir().rename(library(u"TestTool"_s), root(u"moved-away"_s)));
        if (!makeJunction(library(u"TestTool"_s), outside.path()))
            QSKIP("cannot create a junction here");

        QCOMPARE(m_uninstaller->uninstall(u"test-tool"_s).error, UninstallError::ProtectedPath);
        QCOMPARE(readFile(outside.filePath(u"sentinel.txt"_s)), QByteArray("keep"));
        QVERIFY(m_installed->contains(u"test-tool"_s));
        fileops::removeTree(library(u"TestTool"_s));
    }

    void uninstall_recordWithAFolderOutsideTheLibrary()
    {
        // A hand-edited state file must never make the launcher delete something else.
        QTemporaryDir outside;
        QVERIFY(writeFile(outside.filePath(u"sentinel.txt"_s), "keep"));
        QJsonObject record {{u"version"_s, u"1.0.0"_s}, {u"tag"_s, u"v1"_s},
            {u"executable"_s, u"bin/Tool.exe"_s}, {u"installed_at"_s, u"2026-01-01T00:00:00Z"_s}};
        for (const QString& folder : {u".."_s, outside.path(), u"..\\x"_s, u"a/b"_s, u"CON"_s}) {
            record.insert(u"install_directory"_s, folder);
            const QJsonObject document {
                {u"schema_version"_s, 1}, {u"projects"_s, QJsonObject {{u"test-tool"_s, record}}}};
            QVERIFY(writeFile(stateFile(), QJsonDocument(document).toJson()));

            InstalledProjects projects(stateFile());
            projects.load();
            QVERIFY2(!projects.contains(u"test-tool"_s), qPrintable(folder)); // dropped on load
            Uninstaller uninstaller(*m_layout, projects);
            QCOMPARE(uninstaller.uninstall(u"test-tool"_s).error, UninstallError::NotInstalled);
        }
        QCOMPARE(readFile(outside.filePath(u"sentinel.txt"_s)), QByteArray("keep"));
    }

    // === the list of installed projects ===================================

    InstalledProject sample(const QString& id, const QString& version = u"1.0.0"_s)
    {
        InstalledProject project;
        project.projectId = id;
        project.version = version;
        project.tag = u"v"_s + version;
        project.installDirectory = u"Dir-"_s + id;
        project.executable = u"App.exe"_s;
        project.installedAt = utc(u"2026-05-06T07:08:09Z"_s);
        return project;
    }

    void list_roundTrip()
    {
        const QString file = root(u"state/list.json"_s);
        InstalledProjects list(file);
        QCOMPARE(list.load(), InstalledProjects::LoadResult::CreatedEmpty);

        QSignalSpy changed(&list, &InstalledProjects::changed);
        QVERIFY(list.upsert(sample(u"beta"_s)));
        QVERIFY(list.upsert(sample(u"alpha"_s, u"2.1.0-rc.1+b5"_s)));
        QCOMPARE(changed.size(), 2);

        InstalledProjects again(file);
        QCOMPARE(again.load(), InstalledProjects::LoadResult::Loaded);
        QCOMPARE(again.all().size(), 2);
        QCOMPARE(again.all().first().projectId, u"alpha"_s);
        QCOMPARE(again.get(u"alpha"_s).value(), sample(u"alpha"_s, u"2.1.0-rc.1+b5"_s));
        QVERIFY(!again.get(u"gamma"_s).has_value());

        QVERIFY(list.remove(u"alpha"_s));
        QVERIFY(list.remove(u"alpha"_s)); // already gone: not an error
        InstalledProjects third(file);
        third.load();
        QCOMPARE(third.all().size(), 1);
    }

    void list_keepsForeignKeys()
    {
        QVERIFY(writeFile(
            stateFile(), R"({"schema_version":1,"launcher":{"last_update_check":"x"},"projects":{}})"));
        InstalledProjects list(stateFile());
        list.load();
        QVERIFY(list.upsert(sample(u"alpha"_s)));
        const QJsonObject saved = QJsonDocument::fromJson(readFile(stateFile())).object();
        QCOMPARE(saved.value(u"launcher"_s).toObject().value(u"last_update_check"_s).toString(), u"x"_s);
        QVERIFY(saved.value(u"projects"_s).toObject().contains(u"alpha"_s));
    }

    void list_damagedFileIsKeptAside()
    {
        QVERIFY(writeFile(stateFile(), "{ this is not json"));
        InstalledProjects list(stateFile());
        QCOMPARE(list.load(), InstalledProjects::LoadResult::RecoveredFromCorruption);
        QVERIFY(list.all().isEmpty());
        QVERIFY(!list.backupPath().isEmpty());
        QCOMPARE(readFile(list.backupPath()), QByteArray("{ this is not json"));
        QVERIFY(!QFileInfo::exists(stateFile()));
    }

    void list_newerSchemaIsKeptAside()
    {
        QVERIFY(writeFile(stateFile(), R"({"schema_version":99,"projects":{"alpha":{}}})"));
        InstalledProjects list(stateFile());
        QCOMPARE(list.load(), InstalledProjects::LoadResult::RecoveredFromNewerSchema);
        QVERIFY(list.all().isEmpty());
        QVERIFY(QFileInfo::exists(list.backupPath()));
    }

    void list_dropsInvalidRecords()
    {
        const QByteArray good =
            R"({"version":"1.0.0","tag":"v1","install_directory":"Good","executable":"a.exe","installed_at":"2026-01-01T00:00:00Z"})";
        QVERIFY(writeFile(stateFile(),
            QByteArray(R"({"schema_version":1,"projects":{)") + R"("good":)" + good + R"(,)" + R"("Bad-Id":)"
                + good + R"(,)"
                + R"("bad-version":{"version":"x","install_directory":"A","executable":"a.exe"},)"
                + R"("bad-dir":{"version":"1.0.0","install_directory":"..","executable":"a.exe"},)"
                + R"("bad-dir2":{"version":"1.0.0","install_directory":"C:\\x","executable":"a.exe"},)"
                + R"("bad-exe":{"version":"1.0.0","install_directory":"A","executable":"../a.exe"},)"
                + R"("not-object":5)" + R"(}})"));
        InstalledProjects list(stateFile());
        QCOMPARE(list.load(), InstalledProjects::LoadResult::Loaded);
        QCOMPARE(list.all().size(), 1);
        QVERIFY(list.contains(u"good"_s));
    }

    void list_failedSaveKeepsTheList()
    {
        InstalledProjects list(stateFile());
        list.load();
        QVERIFY(list.upsert(sample(u"alpha"_s)));
        QVERIFY(QFile::remove(stateFile()));
        QVERIFY(QDir().mkpath(stateFile())); // saving cannot replace a folder

        QString error;
        QVERIFY(!list.upsert(sample(u"alpha"_s, u"9.0.0"_s), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(list.get(u"alpha"_s)->version, u"1.0.0"_s);
        QVERIFY(!list.upsert(sample(u"beta"_s)));
        QVERIFY(!list.contains(u"beta"_s));
        QVERIFY(!list.remove(u"alpha"_s));
        QVERIFY(list.contains(u"alpha"_s));
    }

    void list_recordStart()
    {
        InstalledProjects list(stateFile());
        list.load();
        QVERIFY(list.upsert(sample(u"alpha"_s)));
        const QDateTime when = utc(u"2026-06-01T12:00:00Z"_s);
        list.recordStart(u"alpha"_s, when);
        list.recordStart(u"unknown"_s, when);
        QCOMPARE(list.get(u"alpha"_s)->lastStartedAt, when);

        InstalledProjects again(stateFile());
        again.load();
        QCOMPARE(again.get(u"alpha"_s)->lastStartedAt, when);
    }

    void list_concurrentWriters()
    {
        InstalledProjects list(stateFile());
        list.load();
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&list, t, this] {
                for (int i = 0; i < 15; ++i)
                    list.upsert(sample(u"p%1-%2"_s.arg(t).arg(i)));
            });
        }
        for (std::thread& thread : threads)
            thread.join();
        QCOMPARE(list.all().size(), 60);

        InstalledProjects again(stateFile());
        again.load();
        QCOMPARE(again.all().size(), 60);
    }

    // === folders and files =================================================

    void layout_paths()
    {
        const LibraryLayout layout(u"C:/root/library/"_s, u"C:/root/temp"_s);
        QCOMPARE(layout.projectDir(u"Tool"_s), u"C:/root/library/Tool"_s);
        QCOMPARE(layout.currentDir(u"Tool"_s), u"C:/root/library/Tool/current"_s);
        QCOMPARE(layout.backupsDir(u"Tool"_s), u"C:/root/library/Tool/backups"_s);
        QCOMPARE(layout.stagingDir(u"my-id"_s), u"C:/root/temp/staging-my-id"_s);
        QCOMPARE(layout.trashDir(u"my-id"_s), u"C:/root/temp/uninstall-my-id"_s);
        InstalledProject project;
        project.installDirectory = u"Tool"_s;
        project.executable = u"bin/Tool.exe"_s;
        QCOMPARE(layout.executablePath(project), u"C:/root/library/Tool/current/bin/Tool.exe"_s);
    }

    void layout_cleanupTemp()
    {
        QVERIFY(writeFile(temp(u"staging-a/file.txt"_s), "x"));
        QVERIFY(writeFile(temp(u"uninstall-b/deep/file.txt"_s), "x"));
        QVERIFY(writeFile(temp(u"download-1.part"_s), "partial"));
        QVERIFY(writeFile(temp(u"other/file.txt"_s), "x"));
        // What a launcher update leaves: the staged release and the updater's working folder.
        QVERIFY(writeFile(temp(u"update-2.0.0/KERJAMI.exe"_s), "x"));
        QVERIFY(writeFile(temp(u"updater-2.0.0/job.json"_s), "{}"));

        QCOMPARE(m_layout->cleanupTemp(), 4);
        QStringList rest = leftovers();
        rest.sort();
        QCOMPARE(rest, (QStringList {u"download-1.part"_s, u"other"_s}));
    }

    void fileops_removeTree()
    {
        QVERIFY(fileops::removeTree(root(u"does-not-exist"_s))); // nothing to remove is fine

        QVERIFY(writeFile(root(u"tree/a/b/c.txt"_s), "x"));
        QVERIFY(writeFile(root(u"tree/ro.txt"_s), "x"));
        QVERIFY(QFile::setPermissions(root(u"tree/ro.txt"_s), QFileDevice::ReadOwner));
        QString error = u"stale"_s;
        QVERIFY(fileops::removeTree(root(u"tree"_s), &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!QFileInfo::exists(root(u"tree"_s)));
    }

    void fileops_isLink()
    {
        QTemporaryDir outside;
        QVERIFY(QDir().mkpath(root(u"plain"_s)));
        QVERIFY(!fileops::isLink(root(u"plain"_s)));
        QVERIFY(!fileops::isLink(root(u"missing"_s)));
        if (!makeJunction(root(u"junction"_s), outside.path()))
            QSKIP("cannot create a junction here");
        QVERIFY(fileops::isLink(root(u"junction"_s)));
        QVERIFY(fileops::removeTree(root(u"junction"_s)));
        QVERIFY(QFileInfo::exists(outside.path()));
    }

    void fileops_renameFailureIsReported()
    {
        QString error;
        QVERIFY(!fileops::renameWithRetry(root(u"missing"_s), root(u"target"_s), &error));
        QVERIFY(!error.isEmpty());
    }

    // === start =================================================================

    void start_refusals()
    {
        QCOMPARE(
            launcher::startProject(*m_layout, *m_installed, u"test-tool"_s).error, StartError::NotInstalled);

        QVERIFY(installAndWait(releaseRequest(u"1.0.0"_s)).ok());
        QVERIFY(QFile::remove(current(u"bin/Tool.exe"_s)));
        QCOMPARE(
            launcher::startProject(*m_layout, *m_installed, u"test-tool"_s).error, StartError::FilesMissing);
    }

#ifdef Q_OS_WIN
    void start_runsTheInstalledProgram()
    {
        // A harmless program that ships with Windows stands in for a project (find.exe
        // prints a usage line and exits).
        const QString system = qEnvironmentVariable("SystemRoot") + u"/System32/find.exe"_s;
        if (!QFileInfo::exists(system))
            QSKIP("find.exe not found");
        const QByteArray exe = readFile(system);
        QVERIFY(!exe.isEmpty());

        QVERIFY(installAndWait(makeRequest({testzip::file(u"bin/Tool.exe"_s, exe)}, u"1.0.0"_s)).ok());
        const auto result = launcher::startProject(*m_layout, *m_installed, u"test-tool"_s);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QVERIFY(result.processId > 0);
        QVERIFY(m_installed->get(u"test-tool"_s)->lastStartedAt.isValid());
    }
#endif
};

QTEST_GUILESS_MAIN(InstallerTests)
#include "InstallerTests.moc"
