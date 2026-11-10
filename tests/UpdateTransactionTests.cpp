// The update manifest and the all-or-nothing replacement of the launcher's
// program files: success, every way it can fail, an update that is killed in
// the middle, and the guarantee that the user's data is never touched.

#include "core/JsonFile.h"
#include "core/PathManager.h"
#include "installer/FileOps.h"
#include "security/HashVerifier.h"
#include "updater/UpdateManifest.h"
#include "updater/UpdateStartup.h"
#include "updater/UpdateTransaction.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <windows.h>

#include <map>
#include <memory>
#include <thread>

using namespace Qt::StringLiterals;
using launcher::ManifestExpectations;
using launcher::ManifestFile;
using launcher::TransactionOutcome;
using launcher::TransactionPlan;
using launcher::TransactionResult;
using launcher::UpdateManifest;
namespace format = launcher::UpdateManifestFormat;
namespace transaction = launcher::UpdateTransaction;

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

bool makeJunction(const QString& link, const QString& target)
{
    QProcess process;
    process.start(u"cmd.exe"_s,
        {u"/c"_s, u"mklink"_s, u"/J"_s, QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)});
    return process.waitForFinished(10000) && process.exitCode() == 0;
}

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

/// Everything under a folder as path -> content, for comparing states.
Files snapshot(const QString& root, const QStringList& skipTop = {})
{
    Files files;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = QDir(root).relativeFilePath(it.next());
        const QString top = path.section(u'/', 0, 0);
        if (skipTop.contains(top, Qt::CaseInsensitive))
            continue;
        files[path] = readFile(root + u'/' + path);
    }
    return files;
}

const ManifestExpectations kExpect {u"KERJAMI"_s, u"KERJAMI.exe"_s, u"LauncherUpdater.exe"_s};

/// A valid manifest as a JSON object, for the tests that spoil one thing at a time.
QJsonObject validManifest()
{
    UpdateManifest manifest;
    manifest.schemaVersion = 1;
    manifest.product = u"KERJAMI"_s;
    manifest.version = u"2.0.0"_s;
    manifest.launcher = u"KERJAMI.exe"_s;
    manifest.updater = u"LauncherUpdater.exe"_s;
    manifest.files = {{u"KERJAMI.exe"_s, 10, sha256("exe")}, {u"LauncherUpdater.exe"_s, 5, sha256("upd")}};
    return QJsonDocument::fromJson(format::toJson(manifest)).object();
}

QByteArray toBytes(const QJsonObject& object)
{
    return QJsonDocument(object).toJson();
}

} // namespace

class UpdateTransactionTests : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_dir;

    QString root() const { return m_dir->filePath(u"app"_s); }
    QString staging() const { return root() + u"/temp/update-2.0.0"_s; }

    /// The files of the installed version, and the user's data around them.
    static Files oldProgram()
    {
        return {{u"KERJAMI.exe"_s, "old launcher"}, {u"LauncherUpdater.exe"_s, "old updater"},
            {u"extra.dll"_s, "old extra"}, {u"qt.conf"_s, "[Paths]"}, {u"obsolete.dll"_s, "obsolete"},
            {u"resources/theme.qss"_s, "old theme"}};
    }
    static Files userData()
    {
        return {{u"launcher/settings.json"_s, "{\"theme\":\"dark\"}"},
            {u"launcher/launcher.json"_s, "{\"projects\":{}}"},
            {u"library/tool/current/Tool.exe"_s, "a project"},
            {u"library/tool/backups/1.0/Tool.exe"_s, "backup"}, {u"downloads/tool/Tool.zip"_s, "download"},
            {u"cache/registry.json"_s, "{}"}, {u"logs/launcher.log"_s, "log"}};
    }
    static Files newProgram()
    {
        return {{u"KERJAMI.exe"_s, "NEW launcher"}, {u"LauncherUpdater.exe"_s, "NEW updater"},
            {u"extra.dll"_s, "NEW extra"}, {u"qt.conf"_s, "[Paths]"}, // unchanged
            {u"resources/theme.qss"_s, "NEW theme"}, {u"resources/added/new.txt"_s, "a file that is new"}};
    }

    void install(const Files& files)
    {
        for (const auto& [path, content] : files)
            QVERIFY(writeFile(root() + u'/' + path, content));
    }

    /// A plan to replace the program with @p fresh, staged and listed in the manifest.
    TransactionPlan makePlan(const Files& fresh, const QStringList& remove = {u"obsolete.dll"_s})
    {
        TransactionPlan plan;
        plan.rootDir = root();
        plan.stagingDir = staging();
        plan.backupDir = root() + u"/updater/backups/1.0.0-test"_s;
        plan.journalFile = root() + u"/updater/journal.json"_s;
        plan.fromVersion = u"1.0.0"_s;
        plan.toVersion = u"2.0.0"_s;
        plan.manifest.schemaVersion = 1;
        plan.manifest.product = u"KERJAMI"_s;
        plan.manifest.version = u"2.0.0"_s;
        plan.manifest.launcher = u"KERJAMI.exe"_s;
        plan.manifest.updater = u"LauncherUpdater.exe"_s;
        plan.manifest.remove = remove;
        for (const auto& [path, content] : fresh) {
            [&] { QVERIFY(writeFile(staging() + u'/' + path, content)); }();
            plan.manifest.files.append({path, content.size(), sha256(content)});
        }
        return plan;
    }

    Files programNow()
    {
        return snapshot(root(),
            {u"launcher"_s, u"library"_s, u"downloads"_s, u"cache"_s, u"logs"_s, u"temp"_s, u"updater"_s});
    }
    Files dataNow()
    {
        return snapshot(root(),
            {u"KERJAMI.exe"_s, u"LauncherUpdater.exe"_s, u"extra.dll"_s, u"qt.conf"_s, u"obsolete.dll"_s,
                u"resources"_s, u"temp"_s, u"updater"_s});
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        install(oldProgram());
        install(userData());
    }

    void cleanup() { m_dir.reset(); }

    // === the manifest ===========================================================

    void manifest_roundTrip()
    {
        const auto parsed = format::parse(toBytes(validManifest()), kExpect);
        QVERIFY2(parsed.ok, qPrintable(parsed.error));
        QCOMPARE(parsed.manifest.version, u"2.0.0"_s);
        QCOMPARE(parsed.manifest.files.size(), 2);
        QCOMPARE(parsed.manifest.find(u"launcherupdater.EXE"_s)->size, qint64(5)); // case does not matter
        QCOMPARE(format::toJson(parsed.manifest),
            format::toJson(format::parse(format::toJson(parsed.manifest), kExpect).manifest));

        QJsonObject withRemove = validManifest();
        withRemove.insert(u"remove"_s, QJsonArray {u"old.dll"_s, u"sub/old2.dll"_s});
        const auto parsedRemove = format::parse(toBytes(withRemove), kExpect);
        QVERIFY2(parsedRemove.ok, qPrintable(parsedRemove.error));
        QCOMPARE(parsedRemove.manifest.remove, (QStringList {u"old.dll"_s, u"sub/old2.dll"_s}));
    }

    void manifest_acceptsUppercaseHashAndSubfolders()
    {
        QJsonObject object = validManifest();
        QJsonArray files = object.value(u"files"_s).toArray();
        files.append(QJsonObject {{u"path"_s, u"resources/styles/app.qss"_s}, {u"size"_s, 1},
            {u"sha256"_s, sha256("x").toUpper()}});
        object.insert(u"files"_s, files);
        const auto parsed = format::parse(toBytes(object), kExpect);
        QVERIFY2(parsed.ok, qPrintable(parsed.error));
        QCOMPARE(
            parsed.manifest.find(u"resources/styles/app.qss"_s)->sha256, sha256("x")); // stored lowercase
    }

    void manifest_refusals_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("fragment");

        const auto with = [](const QString& key, const QJsonValue& value) {
            QJsonObject object = validManifest();
            object.insert(key, value);
            return toBytes(object);
        };
        const auto withFile = [](const QJsonObject& file) {
            QJsonObject object = validManifest();
            QJsonArray files = object.value(u"files"_s).toArray();
            files.append(file);
            object.insert(u"files"_s, files);
            return toBytes(object);
        };
        const auto file = [](const QString& path) {
            return QJsonObject {{u"path"_s, path}, {u"size"_s, 3}, {u"sha256"_s, sha256("abc")}};
        };

        QTest::newRow("not json") << QByteArray("{ nope") << u"JSON"_s;
        QTest::newRow("an array") << QByteArray("[1,2]") << u"JSON"_s;
        QTest::newRow("empty") << QByteArray() << u"JSON"_s;
        QTest::newRow("schema missing") << toBytes([] {
            QJsonObject o = validManifest();
            o.remove(u"schema_version"_s);
            return o;
        }()) << u"schema"_s;
        QTest::newRow("schema zero") << with(u"schema_version"_s, 0) << u"schema"_s;
        QTest::newRow("schema from the future") << with(u"schema_version"_s, 2) << u"schema version 2"_s;
        QTest::newRow("other product") << with(u"product"_s, u"Other"_s) << u"Other"_s;
        QTest::newRow("version word") << with(u"version"_s, u"latest"_s) << u"not a version"_s;
        QTest::newRow("version with v") << with(u"version"_s, u"v2.0.0"_s) << u"not a version"_s;
        QTest::newRow("launcher renamed") << with(u"launcher"_s, u"Evil.exe"_s) << u"launcher"_s;
        QTest::newRow("updater renamed") << with(u"updater"_s, u"Evil.exe"_s) << u"updater"_s;
        QTest::newRow("launcher traversal") << with(u"launcher"_s, u"../KERJAMI.exe"_s) << u"launcher"_s;
        QTest::newRow("files not a list") << with(u"files"_s, u"x"_s) << u"files"_s;
        QTest::newRow("files empty") << with(u"files"_s, QJsonArray {}) << u"files"_s;
        QTest::newRow("file is a string")
            << with(u"files"_s, QJsonArray {u"KERJAMI.exe"_s}) << u"not an object"_s;

        // paths
        QTest::newRow("traversal") << withFile(file(u"../evil.dll"_s)) << u"evil"_s;
        QTest::newRow("deep traversal") << withFile(file(u"a/../../evil.dll"_s)) << u"evil"_s;
        QTest::newRow("absolute") << withFile(file(u"/evil.dll"_s)) << u"evil"_s;
        QTest::newRow("drive") << withFile(file(u"C:/evil.dll"_s)) << u"evil"_s;
        QTest::newRow("backslash") << withFile(file(u"sub\\evil.dll"_s)) << u"backslash"_s;
        QTest::newRow("stream") << withFile(file(u"a.dll:hidden"_s)) << u"a.dll"_s;
        QTest::newRow("reserved") << withFile(file(u"nul.txt"_s)) << u"nul"_s;
        QTest::newRow("a folder") << withFile(file(u"sub/"_s)) << u"sub"_s;
        QTest::newRow("current dir") << withFile(file(u"./a.dll"_s)) << u"a.dll"_s;
        QTest::newRow("empty path") << withFile(file(QString())) << u"file"_s;

        // folders of the user's data
        QTest::newRow("settings") << withFile(file(u"launcher/settings.json"_s)) << u"must not touch"_s;
        QTest::newRow("library") << withFile(file(u"library/tool/current/Tool.exe"_s)) << u"must not touch"_s;
        QTest::newRow("downloads") << withFile(file(u"downloads/x.zip"_s)) << u"must not touch"_s;
        QTest::newRow("temp") << withFile(file(u"temp/x"_s)) << u"must not touch"_s;
        QTest::newRow("cache") << withFile(file(u"cache/registry.json"_s)) << u"must not touch"_s;
        QTest::newRow("logs") << withFile(file(u"logs/launcher.log"_s)) << u"must not touch"_s;
        QTest::newRow("updater folder") << withFile(file(u"updater/journal.json"_s)) << u"must not touch"_s;
        QTest::newRow("other case") << withFile(file(u"LAUNCHER/Settings.JSON"_s)) << u"must not touch"_s;
        QTest::newRow("the manifest") << withFile(file(u"update-manifest.json"_s)) << u"must not touch"_s;

        QTest::newRow("twice") << withFile(file(u"KERJAMI.exe"_s)) << u"twice"_s;
        QTest::newRow("twice, other case") << withFile(file(u"kerjami.EXE"_s)) << u"twice"_s;
        QTest::newRow("bad hash") << withFile(
            QJsonObject {{u"path"_s, u"a.dll"_s}, {u"size"_s, 3}, {u"sha256"_s, u"abc"_s}})
                                  << u"sha256"_s;
        QTest::newRow("hash with prefix") << withFile(
            QJsonObject {{u"path"_s, u"a.dll"_s}, {u"size"_s, 3}, {u"sha256"_s, u"sha256:"_s + sha256("a")}})
                                          << u"sha256"_s;
        QTest::newRow("negative size")
            << withFile(QJsonObject {{u"path"_s, u"a.dll"_s}, {u"size"_s, -1}, {u"sha256"_s, sha256("a")}})
            << u"size"_s;
        QTest::newRow("fractional size")
            << withFile(QJsonObject {{u"path"_s, u"a.dll"_s}, {u"size"_s, 1.5}, {u"sha256"_s, sha256("a")}})
            << u"size"_s;
        QTest::newRow("huge size") << withFile(
            QJsonObject {{u"path"_s, u"a.dll"_s}, {u"size"_s, 9e15}, {u"sha256"_s, sha256("a")}})
                                   << u"size"_s;
        QTest::newRow("size as text") << withFile(
            QJsonObject {{u"path"_s, u"a.dll"_s}, {u"size"_s, u"3"_s}, {u"sha256"_s, sha256("a")}})
                                      << u"size"_s;

        QTest::newRow("remove traversal") << with(u"remove"_s, QJsonArray {u"../x.dll"_s}) << u"remove"_s;
        QTest::newRow("remove in user data")
            << with(u"remove"_s, QJsonArray {u"library/a/current/a.exe"_s}) << u"remove"_s;
        QTest::newRow("remove a file that is installed")
            << with(u"remove"_s, QJsonArray {u"KERJAMI.exe"_s}) << u"both"_s;
        QTest::newRow("remove not a list") << with(u"remove"_s, u"a.dll"_s) << u"remove"_s;
    }

    void manifest_refusals()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, fragment);
        const auto parsed = format::parse(json, kExpect);
        QVERIFY2(!parsed.ok, "accepted");
        QVERIFY2(parsed.error.contains(fragment, Qt::CaseInsensitive), qPrintable(parsed.error));
    }

    void manifest_limits()
    {
        QJsonObject object = validManifest();
        QJsonArray files = object.value(u"files"_s).toArray();
        for (int i = 0; i < format::kMaxFiles; ++i)
            files.append(
                QJsonObject {{u"path"_s, u"f%1.dll"_s.arg(i)}, {u"size"_s, 1}, {u"sha256"_s, sha256("a")}});
        object.insert(u"files"_s, files);
        QVERIFY(!format::parse(toBytes(object), kExpect).ok); // 202 entries
    }

    void manifest_protectedPaths_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("protectedPath");
        QTest::newRow("exe") << u"KERJAMI.exe"_s << false;
        QTest::newRow("subfolder") << u"resources/a.qss"_s << false;
        QTest::newRow("similar name") << u"launcher.dll"_s << false;
        QTest::newRow("similar folder") << u"library2/a"_s << false;
        QTest::newRow("launcher folder") << u"launcher/settings.json"_s << true;
        QTest::newRow("the folder itself") << u"launcher"_s << true;
        QTest::newRow("library") << u"Library/x"_s << true;
        QTest::newRow("downloads") << u"DOWNLOADS/x"_s << true;
        QTest::newRow("temp") << u"temp/x"_s << true;
        QTest::newRow("cache") << u"cache/x"_s << true;
        QTest::newRow("logs") << u"logs/x"_s << true;
        QTest::newRow("updater") << u"updater/backups/x"_s << true;
        QTest::newRow("manifest") << u"Update-Manifest.json"_s << true;
    }

    void manifest_protectedPaths()
    {
        QFETCH(QString, path);
        QFETCH(bool, protectedPath);
        QCOMPARE(format::isProtectedPath(path), protectedPath);
    }

    // === replacing the files ======================================================

    void apply_replacesTheProgramAndLeavesTheUsersDataAlone()
    {
        const Files dataBefore = dataNow();
        TransactionPlan plan = makePlan(newProgram());
        const TransactionResult result = transaction::apply(plan);
        QVERIFY2(result.ok(), qPrintable(result.detail));

        // New program, in place.
        Files expected = newProgram();
        QCOMPARE(programNow(), expected);
        // The obsolete file is gone, the replaced ones are in the backup, the unchanged one was not touched.
        QVERIFY(!QFileInfo::exists(root() + u"/obsolete.dll"_s));
        QCOMPARE(readFile(plan.backupDir + u"/KERJAMI.exe"_s), QByteArray("old launcher"));
        QCOMPARE(readFile(plan.backupDir + u"/LauncherUpdater.exe"_s), QByteArray("old updater"));
        QCOMPARE(readFile(plan.backupDir + u"/extra.dll"_s), QByteArray("old extra"));
        QCOMPARE(readFile(plan.backupDir + u"/obsolete.dll"_s), QByteArray("obsolete"));
        QCOMPARE(readFile(plan.backupDir + u"/resources/theme.qss"_s), QByteArray("old theme"));
        QVERIFY(!QFileInfo::exists(plan.backupDir + u"/qt.conf"_s));
        QCOMPARE(result.filesChanged, 6); // five replaced or added (qt.conf was already right), one removed

        // The user's data is byte for byte as before.
        QCOMPARE(dataNow(), dataBefore);
        QVERIFY(!QFileInfo::exists(plan.journalFile));
    }

    void apply_nothingToDoIsSuccess()
    {
        install(newProgram());
        QVERIFY(QFile::remove(root() + u"/obsolete.dll"_s));
        TransactionPlan plan = makePlan(newProgram());
        const TransactionResult result = transaction::apply(plan);
        QVERIFY(result.ok());
        QCOMPARE(result.filesChanged, 0);
        QVERIFY(!QFileInfo::exists(plan.backupDir));
        QVERIFY(!QFileInfo::exists(plan.journalFile));
    }

    void apply_twiceIsHarmless()
    {
        TransactionPlan plan = makePlan(newProgram());
        QVERIFY(transaction::apply(plan).ok());
        TransactionPlan again = makePlan(newProgram());
        again.backupDir = root() + u"/updater/backups/second"_s;
        const TransactionResult result = transaction::apply(again);
        QVERIFY(result.ok());
        QCOMPARE(result.filesChanged, 0);
    }

    void apply_launcherIsReplacedLast()
    {
        // At every step, look at which files are new. The launcher program must be the last to change.
        TransactionPlan plan = makePlan(newProgram());
        QStringList order;
        plan.simulateCrashAfter = [&](int) {
            const Files now = programNow();
            for (const auto& [path, content] : now) {
                if (content.startsWith("NEW") || path.endsWith(u"new.txt"_s)) {
                    if (!order.contains(path))
                        order.append(path);
                }
            }
            return false;
        };
        QVERIFY(transaction::apply(plan).ok());
        QVERIFY(!order.isEmpty());
        QCOMPARE(order.last(), u"KERJAMI.exe"_s);
        QCOMPARE(order.at(order.size() - 2), u"LauncherUpdater.exe"_s);
    }

    // === refused before anything changes =============================================

    void apply_refusesAStagedFileThatDoesNotMatch()
    {
        const Files programBefore = programNow();
        const Files dataBefore = dataNow();
        TransactionPlan plan = makePlan(newProgram());
        QVERIFY(writeFile(staging() + u"/extra.dll"_s, "TAMPERED"));

        const TransactionResult result = transaction::apply(plan);
        QCOMPARE(result.outcome, TransactionOutcome::NotApplied);
        QCOMPARE(result.failedPath, u"extra.dll"_s);
        QCOMPARE(programNow(), programBefore);
        QCOMPARE(dataNow(), dataBefore);
        QVERIFY(!QFileInfo::exists(plan.backupDir));
        QVERIFY(!QFileInfo::exists(plan.journalFile));
    }

    void apply_refusesAMissingStagedFile()
    {
        const Files programBefore = programNow();
        TransactionPlan plan = makePlan(newProgram());
        QVERIFY(QFile::remove(staging() + u"/resources/theme.qss"_s));
        QCOMPARE(transaction::apply(plan).outcome, TransactionOutcome::NotApplied);
        QCOMPARE(programNow(), programBefore);
    }

    void apply_refusesAWrongSizeInTheManifest()
    {
        const Files programBefore = programNow();
        TransactionPlan plan = makePlan(newProgram());
        plan.manifest.files[0].size += 1;
        QCOMPARE(transaction::apply(plan).outcome, TransactionOutcome::NotApplied);
        QCOMPARE(programNow(), programBefore);
    }

    void apply_refusesUnsafeTargets_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("traversal") << u"../evil.txt"_s;
        QTest::newRow("deep traversal") << u"resources/../../evil.txt"_s;
        QTest::newRow("settings") << u"launcher/settings.json"_s;
        QTest::newRow("a project") << u"library/tool/current/Tool.exe"_s;
        QTest::newRow("downloads") << u"downloads/tool/Tool.zip"_s;
        QTest::newRow("absolute") << u"C:/evil.txt"_s;
        QTest::newRow("backslash") << u"resources\\theme.qss"_s;
        QTest::newRow("stream") << u"extra.dll:evil"_s;
    }

    void apply_refusesUnsafeTargets()
    {
        // Even a plan that did not come from the parser cannot reach outside the program files.
        QFETCH(QString, path);
        const Files programBefore = programNow();
        const Files dataBefore = dataNow();
        TransactionPlan plan = makePlan(newProgram());
        plan.manifest.files.append({path, 4, sha256("evil")});

        const TransactionResult result = transaction::apply(plan);
        QCOMPARE(result.outcome, TransactionOutcome::NotApplied);
        QCOMPARE(programNow(), programBefore);
        QCOMPARE(dataNow(), dataBefore);
        QVERIFY(!QFileInfo::exists(m_dir->filePath(u"evil.txt"_s)));
    }

    void apply_refusesToRemoveUserData()
    {
        const Files dataBefore = dataNow();
        TransactionPlan plan = makePlan(newProgram(), {u"launcher/settings.json"_s});
        QCOMPARE(transaction::apply(plan).outcome, TransactionOutcome::NotApplied);
        QCOMPARE(dataNow(), dataBefore);
    }

    void apply_doesNotFollowALinkInTheWay()
    {
        QTemporaryDir outside;
        QVERIFY(writeFile(outside.filePath(u"sentinel.txt"_s), "keep"));
        // "resources" is a normal folder; replace it by a junction to somewhere else.
        QVERIFY(fileOpsRemoveTree(root() + u"/resources"_s));
        if (!makeJunction(root() + u"/resources"_s, outside.path()))
            QSKIP("cannot create a junction here");

        TransactionPlan plan = makePlan(newProgram());
        const TransactionResult result = transaction::apply(plan);
        QCOMPARE(result.outcome, TransactionOutcome::NotApplied);
        QCOMPARE(readFile(outside.filePath(u"sentinel.txt"_s)), QByteArray("keep"));
        QVERIFY(!QFileInfo::exists(outside.filePath(u"theme.qss"_s)));
        QVERIFY(!QFileInfo::exists(root() + u"/KERJAMI.exe.new"_s));
        QCOMPARE(readFile(root() + u"/KERJAMI.exe"_s),
            QByteArray("old launcher")); // not even the first file changed
        launcher::FileOps::removeTree(root() + u"/resources"_s);
    }

    void apply_refusesANonEmptyBackupFolder()
    {
        TransactionPlan plan = makePlan(newProgram());
        QVERIFY(writeFile(plan.backupDir + u"/something"_s, "x"));
        QCOMPARE(transaction::apply(plan).outcome, TransactionOutcome::NotApplied);
        QCOMPARE(readFile(root() + u"/KERJAMI.exe"_s), QByteArray("old launcher"));
    }

    void apply_refusesFoldersOutsideTheRoot()
    {
        TransactionPlan plan = makePlan(newProgram());
        plan.backupDir = m_dir->filePath(u"outside-backup"_s);
        QCOMPARE(transaction::apply(plan).outcome, TransactionOutcome::NotApplied);
        TransactionPlan other = makePlan(newProgram());
        other.stagingDir = m_dir->filePath(u"outside-staging"_s);
        QVERIFY(QDir().mkpath(other.stagingDir));
        QCOMPARE(transaction::apply(other).outcome, TransactionOutcome::NotApplied);
    }

    // === failing in the middle: rollback ==============================================

    void apply_rollsBackWhenAFileIsInUse()
    {
        const Files programBefore = programNow();
        const Files dataBefore = dataNow();
        TransactionPlan plan = makePlan(newProgram());

        // Something holds a file open, as a running program does. Files are replaced
        // in order; this one is in the middle, so earlier ones are already replaced.
        FileLock lock(root() + u"/resources/theme.qss"_s);
        QVERIFY(lock.isLocked());

        const TransactionResult result = transaction::apply(plan);
        QCOMPARE(result.outcome, TransactionOutcome::RolledBack);
        QCOMPARE(result.failedPath, u"resources/theme.qss"_s);
        QVERIFY(result.detail.contains(u"in use"_s));

        // Exactly the old state: every file, every byte; nothing new left behind.
        QCOMPARE(programNow(), programBefore);
        QCOMPARE(dataNow(), dataBefore);
        QVERIFY(!QFileInfo::exists(root() + u"/resources/added"_s));
        QVERIFY(!QFileInfo::exists(plan.journalFile));
        QVERIFY(!QFileInfo::exists(plan.backupDir));
        QCOMPARE(readFile(root() + u"/obsolete.dll"_s), QByteArray("obsolete"));
    }

    void apply_rollsBackWhenTheLauncherItselfIsInUse()
    {
        // The launcher program is the last file; everything before it is rolled back.
        const Files programBefore = programNow();
        TransactionPlan plan = makePlan(newProgram());
        FileLock lock(root() + u"/KERJAMI.exe"_s);
        QVERIFY(lock.isLocked());

        const TransactionResult result = transaction::apply(plan);
        QCOMPARE(result.outcome, TransactionOutcome::RolledBack);
        QCOMPARE(result.failedPath, u"KERJAMI.exe"_s);
        QCOMPARE(programNow(), programBefore);
    }

    void apply_afterARollbackTheNextAttemptWorks()
    {
        TransactionPlan plan = makePlan(newProgram());
        {
            FileLock lock(root() + u"/extra.dll"_s);
            QCOMPARE(transaction::apply(plan).outcome, TransactionOutcome::RolledBack);
        }
        TransactionPlan again = makePlan(newProgram());
        again.backupDir = root() + u"/updater/backups/second"_s;
        QVERIFY(transaction::apply(again).ok());
        QCOMPARE(readFile(root() + u"/KERJAMI.exe"_s), QByteArray("NEW launcher"));
    }

    // === killed in the middle: recovery ================================================

    void recover_withoutAJournalDoesNothing()
    {
        const TransactionResult result = transaction::recover(root(), root() + u"/updater/journal.json"_s);
        QCOMPARE(result.outcome, TransactionOutcome::NothingToDo);
    }

    void recover_rollsBackAnUpdateKilledInTheMiddle()
    {
        const Files programBefore = programNow();
        const Files dataBefore = dataNow();
        TransactionPlan plan = makePlan(newProgram());
        // "Killed" after the second file was placed.
        plan.simulateCrashAfter = [](int step) { return step == 2; };
        QCOMPARE(transaction::apply(plan).outcome, TransactionOutcome::Interrupted);
        QVERIFY(QFileInfo::exists(plan.journalFile));
        QVERIFY(programNow() != programBefore);                                     // a mixed state
        QCOMPARE(readFile(root() + u"/KERJAMI.exe"_s), QByteArray("old launcher")); // still startable

        const TransactionResult result = transaction::recover(root(), plan.journalFile);
        QCOMPARE(result.outcome, TransactionOutcome::RolledBack);
        QCOMPARE(programNow(), programBefore);
        QCOMPARE(dataNow(), dataBefore);
        QVERIFY(!QFileInfo::exists(plan.journalFile));
        QVERIFY(!QFileInfo::exists(root() + u"/resources/added"_s));

        // A second recovery has nothing left to do.
        QCOMPARE(transaction::recover(root(), plan.journalFile).outcome, TransactionOutcome::NothingToDo);
    }

    void recover_rollsBackAfterEveryPossibleStep()
    {
        // Whatever the moment of the crash: back to the old state, or complete.
        const Files programBefore = programNow();
        const int changes =
            5; // KERJAMI.exe, LauncherUpdater.exe, extra.dll, resources/theme.qss, resources/added/new.txt
        for (int crashAt = 1; crashAt <= changes; ++crashAt) {
            m_dir = std::make_unique<QTemporaryDir>();
            install(oldProgram());
            install(userData());
            TransactionPlan plan = makePlan(newProgram());
            plan.simulateCrashAfter = [crashAt](int step) { return step == crashAt; };
            QCOMPARE(transaction::apply(plan).outcome, TransactionOutcome::Interrupted);

            // Even with every file placed the obsolete file is still there, so the update was not
            // complete: back to the old state, whatever the moment of the crash.
            const TransactionResult result = transaction::recover(root(), plan.journalFile);
            QVERIFY2(result.outcome == TransactionOutcome::RolledBack, qPrintable(QString::number(crashAt)));
            QCOMPARE(programNow(), programBefore);
            QVERIFY(!QFileInfo::exists(plan.journalFile));
        }
    }

    void recover_completesAnUpdateThatOnlyLackedItsCleanup()
    {
        // Everything is in place; the process died before it deleted the journal.
        TransactionPlan plan = makePlan(newProgram(), {});
        plan.simulateCrashAfter = [](int step) { return step == 5; };
        QCOMPARE(transaction::apply(plan).outcome, TransactionOutcome::Interrupted);
        QCOMPARE(readFile(root() + u"/KERJAMI.exe"_s), QByteArray("NEW launcher"));

        const TransactionResult result = transaction::recover(root(), plan.journalFile);
        QCOMPARE(result.outcome, TransactionOutcome::Success);
        QCOMPARE(readFile(root() + u"/KERJAMI.exe"_s), QByteArray("NEW launcher"));
        QCOMPARE(readFile(root() + u"/extra.dll"_s), QByteArray("NEW extra"));
        QVERIFY(!QFileInfo::exists(plan.journalFile));
    }

    void recover_unreadableOrUnsafeJournalsAreSetAsideAndTouchNothing()
    {
        const Files programBefore = programNow();
        const Files dataBefore = dataNow();
        const QString journal = root() + u"/updater/journal.json"_s;

        QVERIFY(writeFile(journal, "{ not json"));
        QCOMPARE(transaction::recover(root(), journal).outcome, TransactionOutcome::NotApplied);
        QVERIFY(!QFileInfo::exists(journal));

        // Wrong schema.
        QVERIFY(writeFile(journal, R"({"schema_version":99,"state":"swapping","files":[]})"));
        QCOMPARE(transaction::recover(root(), journal).outcome, TransactionOutcome::NotApplied);

        // A journal that lists a path in the user's data.
        const QString hash = sha256("x");
        QVERIFY(writeFile(journal,
            QJsonDocument(QJsonObject {{u"schema_version"_s, 1}, {u"state"_s, u"swapping"_s},
                              {u"backup_dir"_s, u"updater/backups/x"_s},
                              {u"files"_s,
                                  QJsonArray {QJsonObject {{u"path"_s, u"launcher/settings.json"_s},
                                      {u"sha256"_s, hash}, {u"had_old"_s, false}}}},
                              {u"remove"_s, QJsonArray {}}})
                .toJson()));
        QCOMPARE(transaction::recover(root(), journal).outcome, TransactionOutcome::NotApplied);

        // A journal that lists a path outside the folder.
        QVERIFY(writeFile(journal,
            QJsonDocument(QJsonObject {{u"schema_version"_s, 1}, {u"state"_s, u"swapping"_s},
                              {u"backup_dir"_s, u"../outside"_s}, {u"files"_s, QJsonArray {}},
                              {u"remove"_s, QJsonArray {}}})
                .toJson()));
        QCOMPARE(transaction::recover(root(), journal).outcome, TransactionOutcome::NotApplied);

        QCOMPARE(programNow(), programBefore);
        QCOMPARE(dataNow(), dataBefore);
    }

    void rollbackThatCannotFinishIsRepairedLater()
    {
        // The worst case: a step fails and the rollback is blocked too. The journal
        // stays, and the next start finishes the job.
        const Files programBefore = programNow();
        TransactionPlan plan = makePlan(newProgram());
        FileLock lockOld(root() + u"/resources/theme.qss"_s); // makes this step fail
        std::unique_ptr<FileLock> lockNew; // blocks the removal of a new file during the rollback
        plan.simulateCrashAfter = [&](int step) {
            if (step == 1) // the first placed file is held open from now on
                lockNew = std::make_unique<FileLock>(root() + u"/extra.dll"_s);
            return false;
        };
        const TransactionResult result = transaction::apply(plan);
        QCOMPARE(result.outcome, TransactionOutcome::RollbackFailed);
        QVERIFY(QFileInfo::exists(plan.journalFile));

        // Later, nothing is held any more: the next start repairs the state.
        lockNew.reset();
        lockOld.release();
        const TransactionResult repaired = transaction::recover(root(), plan.journalFile);
        QCOMPARE(repaired.outcome, TransactionOutcome::RolledBack);
        QCOMPARE(programNow(), programBefore);
        QVERIFY(!QFileInfo::exists(plan.journalFile));
    }

    // === the start of the launcher =======================================================

    void startup_withoutAnUpdaterFolderIsQuiet()
    {
        const launcher::PathManager paths(root());
        const auto outcome = launcher::UpdateStartup::run(paths, 100);
        QVERIFY(!outcome.updaterStillRunning);
        QVERIFY(!outcome.recovery.has_value());
        QVERIFY(!outcome.report.has_value());
    }

    void startup_readsTheReportOnceAndRemovesIt()
    {
        const launcher::PathManager paths(root());
        launcher::UpdateReport report;
        report.status = launcher::UpdateReport::Status::RolledBack;
        report.fromVersion = u"1.0.0"_s;
        report.toVersion = u"2.0.0"_s;
        report.failedPath = u"extra.dll"_s;
        report.finishedAt = QDateTime::currentDateTimeUtc();
        QVERIFY(launcher::JsonFile::saveAtomic(paths.updateReportFile(), report.toJson()));

        const auto first = launcher::UpdateStartup::run(paths, 100);
        QVERIFY(first.report.has_value());
        QCOMPARE(first.report->status, launcher::UpdateReport::Status::RolledBack);
        QCOMPARE(first.report->failedPath, u"extra.dll"_s);
        QVERIFY(!QFileInfo::exists(paths.updateReportFile()));

        const auto second = launcher::UpdateStartup::run(paths, 100);
        QVERIFY(!second.report.has_value()); // shown once
    }

    void startup_removesAReportThatCannotBeRead()
    {
        const launcher::PathManager paths(root());
        QVERIFY(writeFile(paths.updateReportFile(), "{ not json"));
        QVERIFY(!launcher::UpdateStartup::run(paths, 100).report.has_value());
        QVERIFY(!QFileInfo::exists(paths.updateReportFile())); // it must not come back at every start

        QVERIFY(writeFile(paths.updateReportFile(), R"({"status":"nonsense"})"));
        QVERIFY(!launcher::UpdateStartup::run(paths, 100).report.has_value());
        QVERIFY(!QFileInfo::exists(paths.updateReportFile()));
    }

    void startup_rollsBackAnInterruptedUpdate()
    {
        const launcher::PathManager paths(root());
        const Files programBefore = programNow();
        TransactionPlan plan = makePlan(newProgram());
        plan.journalFile = paths.updateJournalFile();
        plan.simulateCrashAfter = [](int step) { return step == 2; };
        QCOMPARE(transaction::apply(plan).outcome, TransactionOutcome::Interrupted);
        QVERIFY(programNow() != programBefore);

        const auto outcome = launcher::UpdateStartup::run(paths, 100);
        QVERIFY(outcome.recovery.has_value());
        QCOMPARE(outcome.recovery->outcome, TransactionOutcome::RolledBack);
        QCOMPARE(programNow(), programBefore);
        QVERIFY(!QFileInfo::exists(paths.updateJournalFile()));
    }

    void startup_waitsForAnUpdaterThatIsStillWorking()
    {
        const launcher::PathManager paths(root());
        QVERIFY(QDir().mkpath(paths.updaterDir()));
        QLockFile updaterLock(paths.updaterLockFile());
        QVERIFY(updaterLock.tryLock(0));

        // The updater finishes while the launcher waits: it proceeds.
        std::thread finisher([&updaterLock] {
            QThread::msleep(500);
            updaterLock.unlock();
        });
        QElapsedTimer timer;
        timer.start();
        const auto outcome = launcher::UpdateStartup::run(paths, 10000);
        finisher.join();
        QVERIFY(!outcome.updaterStillRunning);
        QVERIFY(timer.elapsed() >= 400);
        QVERIFY(timer.elapsed() < 8000);
    }

    void startup_givesUpOnAnUpdaterThatDoesNotFinish()
    {
        const launcher::PathManager paths(root());
        const Files programBefore = programNow();
        QVERIFY(QDir().mkpath(paths.updaterDir()));
        QLockFile updaterLock(paths.updaterLockFile());
        QVERIFY(updaterLock.tryLock(0));

        // A journal of the update that is under way must not be touched by this start.
        TransactionPlan plan = makePlan(newProgram());
        plan.journalFile = paths.updateJournalFile();
        plan.simulateCrashAfter = [](int step) { return step == 2; };
        QCOMPARE(transaction::apply(plan).outcome, TransactionOutcome::Interrupted);
        const Files mixedState = programNow();

        const auto outcome = launcher::UpdateStartup::run(paths, 600);
        QVERIFY(outcome.updaterStillRunning);
        QVERIFY(!outcome.recovery.has_value());
        QCOMPARE(programNow(), mixedState);
        QVERIFY(QFileInfo::exists(paths.updateJournalFile()));
    }

private:
    static bool fileOpsRemoveTree(const QString& path) { return launcher::FileOps::removeTree(path); }
};

QTEST_GUILESS_MAIN(UpdateTransactionTests)
#include "UpdateTransactionTests.moc"
