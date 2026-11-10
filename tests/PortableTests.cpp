// Portable operation: every location derives from the root folder, nothing
// absolute is persisted, and an unusable folder is reported instead of worked around.

#include "core/PathManager.h"
#include "core/SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using launcher::PathManager;
using launcher::SettingsManager;

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

/// Runs a program without forwarding its output into the test log.
int runQuiet(const QString& program, const QStringList& arguments)
{
    QProcess process;
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(program, arguments);
    if (!process.waitForFinished(15000) || process.exitStatus() != QProcess::NormalExit)
        return -1;
    return process.exitCode();
}

} // namespace

class PortableTests : public QObject {
    Q_OBJECT

private slots:
    void locationsDeriveFromRoot();
    void rootIsNormalised();
    void prepareCreatesDataDirectories();
    void prepareKeepsExistingData();
    void prepareReportsMissingRoot();
    void prepareReportsBlockedDirectory();
    void prepareReportsRootWithoutWritePermission();
    void portablePathsRoundTrip();
    void portablePathsCannotLeaveRoot_data();
    void portablePathsCannotLeaveRoot();
    void isInside_data();
    void isInside();
    void dataSurvivesMovingTheFolder();
};

void PortableTests::locationsDeriveFromRoot()
{
    const PathManager paths(u"D:/Portable/Launcher"_s);

    QCOMPARE(paths.root(), u"D:/Portable/Launcher"_s);
    QCOMPARE(paths.configDir(), u"D:/Portable/Launcher/launcher"_s);
    QCOMPARE(paths.libraryDir(), u"D:/Portable/Launcher/library"_s);
    QCOMPARE(paths.downloadsDir(), u"D:/Portable/Launcher/downloads"_s);
    QCOMPARE(paths.tempDir(), u"D:/Portable/Launcher/temp"_s);
    QCOMPARE(paths.cacheDir(), u"D:/Portable/Launcher/cache"_s);
    QCOMPARE(paths.logsDir(), u"D:/Portable/Launcher/logs"_s);
    QCOMPARE(paths.settingsFile(), u"D:/Portable/Launcher/launcher/settings.json"_s);
    QCOMPARE(paths.stateFile(), u"D:/Portable/Launcher/launcher/launcher.json"_s);
    QCOMPARE(paths.lockFile(), u"D:/Portable/Launcher/launcher/launcher.lock"_s);

    // Every data folder lies inside the root.
    const QStringList directories = paths.dataDirectories();
    QCOMPARE(directories.size(), 6);
    for (const QString& directory : directories)
        QVERIFY2(PathManager::isInside(paths.root(), directory), qPrintable(directory));
}

void PortableTests::rootIsNormalised()
{
    QCOMPARE(PathManager(u"D:\\Portable\\Launcher\\"_s).root(), u"D:/Portable/Launcher"_s);
    QCOMPARE(PathManager(u"D:/Portable/Other/../Launcher"_s).root(), u"D:/Portable/Launcher"_s);
}

void PortableTests::prepareCreatesDataDirectories()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    // Spaces and non-ASCII characters are common in user folders.
    const QString root = temp.filePath(u"Mein Launcher (portabel) äöü"_s);
    QVERIFY(QDir().mkpath(root));

    const PathManager paths(root);
    const PathManager::PrepareResult result = paths.prepare();
    QVERIFY(result.ok());

    const QStringList directories = paths.dataDirectories();
    for (const QString& directory : directories)
        QVERIFY2(QFileInfo(directory).isDir(), qPrintable(directory));

    // The write probes must not leave files behind.
    QCOMPARE(QDir(root).entryList(QDir::Files | QDir::Hidden).size(), 0);
}

void PortableTests::prepareKeepsExistingData()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const PathManager paths(temp.path());
    QVERIFY(paths.prepare().ok());

    const QString installed = paths.libraryDir() + u"/Demo/current/Demo.exe"_s;
    QVERIFY(writeFile(installed, "binary"));
    QVERIFY(writeFile(paths.settingsFile(), "{}"));

    QVERIFY(paths.prepare().ok());
    QCOMPARE(readFile(installed), QByteArray("binary"));
    QCOMPARE(readFile(paths.settingsFile()), QByteArray("{}"));
}

void PortableTests::prepareReportsMissingRoot()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const PathManager paths(temp.filePath(u"does-not-exist"_s));

    const PathManager::PrepareResult result = paths.prepare();
    QCOMPARE(result.error, PathManager::PrepareError::RootMissing);
    QCOMPARE(result.path, paths.root());
    // A missing root is reported, not created somewhere.
    QVERIFY(!QFileInfo::exists(paths.root()));
}

void PortableTests::prepareReportsBlockedDirectory()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const PathManager paths(temp.path());
    // A file occupies the name of a data folder.
    QVERIFY(writeFile(paths.libraryDir(), "not a folder"));

    const PathManager::PrepareResult result = paths.prepare();
    QCOMPARE(result.error, PathManager::PrepareError::CannotCreateDirectory);
    QCOMPARE(result.path, paths.libraryDir());
}

void PortableTests::prepareReportsRootWithoutWritePermission()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString root = temp.filePath(u"locked"_s);
    QVERIFY(QDir().mkpath(root));
    const QString native = QDir::toNativeSeparators(root);

    // Deny "add file" and "add subfolder" for Everyone (SID S-1-1-0), which
    // includes the current user. This is what a protected install location
    // such as "C:\Program Files" looks like to a standard user.
    if (runQuiet(u"icacls"_s, {native, u"/deny"_s, u"*S-1-1-0:(WD,AD)"_s}) != 0)
        QSKIP("icacls is not available; cannot remove write permission");
    const auto restore = qScopeGuard([&] { runQuiet(u"icacls"_s, {native, u"/remove:d"_s, u"*S-1-1-0"_s}); });

    const PathManager paths(root);
    const PathManager::PrepareResult result = paths.prepare();
    QCOMPARE(result.error, PathManager::PrepareError::NotWritable);
    QCOMPARE(result.path, paths.root());
    // Nothing was created anywhere else as a fallback.
    QCOMPARE(QDir(root).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size(), 0);
}

void PortableTests::portablePathsRoundTrip()
{
    const PathManager paths(u"D:/Portable/Launcher"_s);

    const QString absolute = u"D:/Portable/Launcher/library/KERS_Overlay/current/KERS_Overlay.exe"_s;
    const QString portable = paths.toPortable(absolute);
    QCOMPARE(portable, u"library/KERS_Overlay/current/KERS_Overlay.exe"_s);
    QCOMPARE(paths.fromPortable(portable), absolute);

    // Native separators and different case describe the same file.
    QCOMPARE(paths.toPortable(u"d:\\portable\\launcher\\downloads\\file.zip"_s), u"downloads/file.zip"_s);
    QCOMPARE(paths.fromPortable(u"downloads\\file.zip"_s), u"D:/Portable/Launcher/downloads/file.zip"_s);

    // Paths outside the root have no portable form.
    QCOMPARE(paths.toPortable(u"D:/Portable/Other/file.zip"_s), QString());
    QCOMPARE(paths.toPortable(u"C:/Windows/notepad.exe"_s), QString());
    QCOMPARE(paths.toPortable(paths.root()), QString());
}

void PortableTests::portablePathsCannotLeaveRoot_data()
{
    QTest::addColumn<QString>("portable");

    QTest::newRow("empty") << QString();
    QTest::newRow("parent") << u".."_s;
    QTest::newRow("parent traversal") << u"../outside.txt"_s;
    QTest::newRow("nested traversal") << u"library/../../outside.txt"_s;
    QTest::newRow("backslash traversal") << u"library\\..\\..\\outside.txt"_s;
    QTest::newRow("absolute path") << u"C:/Windows/system32/cmd.exe"_s;
    QTest::newRow("drive-relative path") << u"C:outside.txt"_s;
    QTest::newRow("unc path") << u"//server/share/file.txt"_s;
    QTest::newRow("alternate data stream") << u"library/app.exe:hidden"_s;
    QTest::newRow("root itself") << u"."_s;
}

void PortableTests::portablePathsCannotLeaveRoot()
{
    QFETCH(QString, portable);
    const PathManager paths(u"D:/Portable/Launcher"_s);
    QCOMPARE(paths.fromPortable(portable), QString());
}

void PortableTests::isInside_data()
{
    QTest::addColumn<QString>("base");
    QTest::addColumn<QString>("path");
    QTest::addColumn<bool>("expected");

    const QString base = u"C:/Launcher"_s;
    QTest::newRow("child") << base << u"C:/Launcher/library"_s << true;
    QTest::newRow("deep child") << base << u"C:/Launcher/library/app/current/app.exe"_s << true;
    QTest::newRow("backslashes") << u"C:\\Launcher"_s << u"C:\\Launcher\\library"_s << true;
    QTest::newRow("different case") << base << u"c:/LAUNCHER/Library"_s << true;
    QTest::newRow("base with trailing slash") << u"C:/Launcher/"_s << u"C:/Launcher/library"_s << true;
    QTest::newRow("traversal that stays inside") << base << u"C:/Launcher/library/../cache/x"_s << true;
    QTest::newRow("drive root as base") << u"E:/"_s << u"E:/library"_s << true;

    QTest::newRow("base itself") << base << base << false;
    QTest::newRow("base itself with slash") << base << u"C:/Launcher/"_s << false;
    QTest::newRow("sibling with same prefix") << base << u"C:/LauncherEvil/file.txt"_s << false;
    QTest::newRow("parent") << base << u"C:/"_s << false;
    QTest::newRow("traversal to sibling")
        << u"C:/Launcher/library"_s << u"C:/Launcher/library/../settings.json"_s << false;
    QTest::newRow("traversal out of root") << base << u"C:/Launcher/../../Windows/system32"_s << false;
    QTest::newRow("other drive") << base << u"D:/Launcher/library"_s << false;
    QTest::newRow("empty path") << base << QString() << false;
    QTest::newRow("empty base") << QString() << u"C:/Launcher/library"_s << false;
}

void PortableTests::isInside()
{
    QFETCH(QString, base);
    QFETCH(QString, path);
    QFETCH(bool, expected);
    QCOMPARE(PathManager::isInside(base, path), expected);
}

void PortableTests::dataSurvivesMovingTheFolder()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString first = temp.filePath(u"first drive/Launcher"_s);
    const QString second = temp.filePath(u"second drive/Moved Launcher"_s);
    QVERIFY(QDir().mkpath(first));
    QVERIFY(QDir().mkpath(QFileInfo(second).absolutePath()));

    QString portableExecutable;
    {
        const PathManager paths(first);
        QVERIFY(paths.prepare().ok());

        SettingsManager settings(paths.settingsFile());
        QCOMPARE(settings.load(), SettingsManager::LoadResult::CreatedDefaults);
        QVERIFY(settings.setUpdateCheckIntervalMinutes(180));

        const QString executable = paths.libraryDir() + u"/Demo/current/Demo.exe"_s;
        QVERIFY(writeFile(executable, "binary"));
        portableExecutable = paths.toPortable(executable);
        QCOMPARE(portableExecutable, u"library/Demo/current/Demo.exe"_s);

        // The settings file must not record where the folder currently is.
        const QByteArray stored = readFile(paths.settingsFile());
        QVERIFY(!stored.isEmpty());
        QVERIFY(!stored.contains("first drive"));
        QVERIFY(!stored.contains(temp.path().toUtf8()));
    }

    QVERIFY(QDir().rename(first, second));

    const PathManager moved(second);
    QVERIFY(moved.prepare().ok());

    SettingsManager settings(moved.settingsFile());
    QCOMPARE(settings.load(), SettingsManager::LoadResult::Loaded);
    QCOMPARE(settings.settings().updateCheckIntervalMinutes, 180);

    const QString executable = moved.fromPortable(portableExecutable);
    QVERIFY(PathManager::isInside(moved.root(), executable));
    QCOMPARE(readFile(executable), QByteArray("binary"));
}

QTEST_GUILESS_MAIN(PortableTests)
#include "PortableTests.moc"
