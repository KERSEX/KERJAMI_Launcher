// Settings persistence: immediate atomic saves, recovery from damaged files and
// schema migration.

#include "core/SettingsManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using launcher::LauncherSettings;
using launcher::SettingsManager;
using launcher::StartPage;

namespace {

bool writeFile(const QString& path, const QByteArray& content)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonObject readJson(const QString& path)
{
    return QJsonDocument::fromJson(readFile(path)).object();
}

} // namespace

class SettingsTests : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void firstLoadWritesDefaults();
    void changesArePersistedImmediately();
    void unchangedValueDoesNotWriteOrNotify();
    void intervalIsClamped();
    void outOfRangeIntervalInFileIsClamped();
    void corruptFileIsPreservedAndReplaced_data();
    void corruptFileIsPreservedAndReplaced();
    void wrongValueTypesFallBackToDefaults();
    void fileWithoutVersionIsMigrated();
    void fileOfNewerVersionIsPreservedAndReplaced();
    void unknownKeysSurviveSaving();
    void saveFailureIsReported();
    void noTemporaryFilesAreLeftBehind();

private:
    QString settingsPath() const { return m_dir->filePath(u"launcher/settings.json"_s); }

    std::unique_ptr<QTemporaryDir> m_dir;
};

void SettingsTests::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    QVERIFY(QDir().mkpath(m_dir->filePath(u"launcher"_s)));
}

void SettingsTests::cleanup()
{
    m_dir.reset();
}

void SettingsTests::firstLoadWritesDefaults()
{
    SettingsManager settings(settingsPath());
    QCOMPARE(settings.load(), SettingsManager::LoadResult::CreatedDefaults);
    QCOMPARE(settings.settings(), LauncherSettings());
    QVERIFY(settings.backupPath().isEmpty());

    // New installations start with manual, conservative defaults.
    QCOMPARE(settings.settings().updateCheckIntervalMinutes, 60);
    QCOMPARE(settings.settings().startPage, StartPage::Dashboard);
    QVERIFY(settings.settings().registryAutoCheck);
    QVERIFY(settings.settings().launcherUpdateAutoCheck);

    const QJsonObject stored = readJson(settingsPath());
    QCOMPARE(stored.value("schema_version"_L1).toInt(), SettingsManager::kSchemaVersion);
    QCOMPARE(stored.value("updates"_L1).toObject().value("check_interval_minutes"_L1).toInt(), 60);
    QCOMPARE(stored.value("startup"_L1).toObject().value("start_page"_L1).toString(), u"dashboard"_s);
    QCOMPARE(stored.value("appearance"_L1).toObject().value("theme"_L1).toString(), u"dark"_s);

    // The second start reads the same file without rewriting it.
    SettingsManager again(settingsPath());
    QCOMPARE(again.load(), SettingsManager::LoadResult::Loaded);
    QCOMPARE(again.settings(), LauncherSettings());
}

void SettingsTests::changesArePersistedImmediately()
{
    SettingsManager settings(settingsPath());
    settings.load();
    QSignalSpy changed(&settings, &SettingsManager::settingsChanged);

    QVERIFY(settings.setStartPage(StartPage::Library));
    QVERIFY(settings.setRegistryAutoCheck(false));
    QVERIFY(settings.setLauncherUpdateAutoCheck(false));
    QVERIFY(settings.setUpdateCheckIntervalMinutes(360));
    QCOMPARE(changed.count(), 4);

    // A second manager stands in for the next start of the launcher. It is
    // created while the first one still exists: nothing is deferred to shutdown.
    SettingsManager restarted(settingsPath());
    QCOMPARE(restarted.load(), SettingsManager::LoadResult::Loaded);
    QCOMPARE(restarted.settings().startPage, StartPage::Library);
    QVERIFY(!restarted.settings().registryAutoCheck);
    QVERIFY(!restarted.settings().launcherUpdateAutoCheck);
    QCOMPARE(restarted.settings().updateCheckIntervalMinutes, 360);
}

void SettingsTests::unchangedValueDoesNotWriteOrNotify()
{
    SettingsManager settings(settingsPath());
    settings.load();
    const QDateTime before = QFileInfo(settingsPath()).lastModified();
    QSignalSpy changed(&settings, &SettingsManager::settingsChanged);

    QTest::qWait(30); // make a rewrite visible in the timestamp
    QVERIFY(settings.setRegistryAutoCheck(true));
    QVERIFY(settings.setUpdateCheckIntervalMinutes(60));

    QCOMPARE(changed.count(), 0);
    QCOMPARE(QFileInfo(settingsPath()).lastModified(), before);
}

void SettingsTests::intervalIsClamped()
{
    SettingsManager settings(settingsPath());
    settings.load();

    QVERIFY(settings.setUpdateCheckIntervalMinutes(1));
    QCOMPARE(settings.settings().updateCheckIntervalMinutes, LauncherSettings::kMinCheckIntervalMinutes);

    QVERIFY(settings.setUpdateCheckIntervalMinutes(1'000'000));
    QCOMPARE(settings.settings().updateCheckIntervalMinutes, LauncherSettings::kMaxCheckIntervalMinutes);

    QVERIFY(settings.setUpdateCheckIntervalMinutes(-5));
    QCOMPARE(settings.settings().updateCheckIntervalMinutes, LauncherSettings::kMinCheckIntervalMinutes);
}

void SettingsTests::outOfRangeIntervalInFileIsClamped()
{
    QVERIFY(writeFile(settingsPath(), R"({"schema_version": 1, "updates": {"check_interval_minutes": 0}})"));

    SettingsManager settings(settingsPath());
    QCOMPARE(settings.load(), SettingsManager::LoadResult::Loaded);
    QCOMPARE(settings.settings().updateCheckIntervalMinutes, LauncherSettings::kMinCheckIntervalMinutes);
}

void SettingsTests::corruptFileIsPreservedAndReplaced_data()
{
    QTest::addColumn<QByteArray>("content");

    QTest::newRow("empty file") << QByteArray();
    QTest::newRow("truncated json") << QByteArray(R"({"schema_version": 1, "updates": {"check_inter)");
    QTest::newRow("binary garbage") << QByteArray("\x00\xff\x13\x37 not json", 13);
    QTest::newRow("array instead of object") << QByteArray("[1, 2, 3]");
    QTest::newRow("version is text") << QByteArray(R"({"schema_version": "one"})");
    QTest::newRow("version is negative") << QByteArray(R"({"schema_version": -3})");
}

void SettingsTests::corruptFileIsPreservedAndReplaced()
{
    QFETCH(QByteArray, content);
    QVERIFY(writeFile(settingsPath(), content));

    SettingsManager settings(settingsPath());
    QCOMPARE(settings.load(), SettingsManager::LoadResult::RecoveredFromCorruption);
    QCOMPARE(settings.settings(), LauncherSettings());

    // The damaged file is kept byte for byte under a new name.
    QVERIFY(!settings.backupPath().isEmpty());
    QVERIFY(QFileInfo(settings.backupPath()).fileName().startsWith(u"settings.corrupt-"_s));
    QCOMPARE(readFile(settings.backupPath()), content);

    // A valid file takes its place, so the next start is clean.
    QCOMPARE(readJson(settingsPath()).value("schema_version"_L1).toInt(), SettingsManager::kSchemaVersion);
    SettingsManager again(settingsPath());
    QCOMPARE(again.load(), SettingsManager::LoadResult::Loaded);
}

void SettingsTests::wrongValueTypesFallBackToDefaults()
{
    QVERIFY(writeFile(settingsPath(), R"({
        "schema_version": 1,
        "startup": {"start_page": 7},
        "registry": {"auto_check": "yes"},
        "updates": {"check_interval_minutes": "often", "launcher_auto_check": false}
    })"));

    SettingsManager settings(settingsPath());
    QCOMPARE(settings.load(), SettingsManager::LoadResult::Loaded);

    // Each bad entry falls back on its own; the valid one is kept.
    QCOMPARE(settings.settings().startPage, StartPage::Dashboard);
    QVERIFY(settings.settings().registryAutoCheck);
    QCOMPARE(settings.settings().updateCheckIntervalMinutes, 60);
    QVERIFY(!settings.settings().launcherUpdateAutoCheck);
}

void SettingsTests::fileWithoutVersionIsMigrated()
{
    const QByteArray original =
        R"({"updates": {"check_interval_minutes": 180}, "startup": {"start_page": "library"}})";
    QVERIFY(writeFile(settingsPath(), original));

    SettingsManager settings(settingsPath());
    QCOMPARE(settings.load(), SettingsManager::LoadResult::Migrated);

    // Values survive the migration and the file carries the current version.
    QCOMPARE(settings.settings().updateCheckIntervalMinutes, 180);
    QCOMPARE(settings.settings().startPage, StartPage::Library);
    QCOMPARE(readJson(settingsPath()).value("schema_version"_L1).toInt(), SettingsManager::kSchemaVersion);

    // The pre-migration file is kept as a copy.
    QVERIFY(QFileInfo(settings.backupPath()).fileName().startsWith(u"settings.v0-"_s));
    QCOMPARE(readFile(settings.backupPath()), original);

    SettingsManager again(settingsPath());
    QCOMPARE(again.load(), SettingsManager::LoadResult::Loaded);
}

void SettingsTests::fileOfNewerVersionIsPreservedAndReplaced()
{
    const QByteArray original = R"({"schema_version": 99, "updates": {"cadence": "weekly"}})";
    QVERIFY(writeFile(settingsPath(), original));

    SettingsManager settings(settingsPath());
    QCOMPARE(settings.load(), SettingsManager::LoadResult::RecoveredFromNewerSchema);
    QCOMPARE(settings.settings(), LauncherSettings());

    QVERIFY(QFileInfo(settings.backupPath()).fileName().startsWith(u"settings.v99-"_s));
    QCOMPARE(readFile(settings.backupPath()), original);

    // The replacement is a clean file of this version without the foreign keys.
    const QJsonObject stored = readJson(settingsPath());
    QCOMPARE(stored.value("schema_version"_L1).toInt(), SettingsManager::kSchemaVersion);
    QVERIFY(!stored.value("updates"_L1).toObject().contains("cadence"_L1));
}

void SettingsTests::unknownKeysSurviveSaving()
{
    // Per-project settings share this file. Saving a launcher preference must
    // never reset them.
    QVERIFY(writeFile(settingsPath(), R"({
        "schema_version": 1,
        "projects": {"kers-overlay": {"update_mode": "automatic"}},
        "updates": {"check_interval_minutes": 60, "extra": true}
    })"));

    SettingsManager settings(settingsPath());
    QCOMPARE(settings.load(), SettingsManager::LoadResult::Loaded);
    QVERIFY(settings.setUpdateCheckIntervalMinutes(30));

    const QJsonObject stored = readJson(settingsPath());
    QCOMPARE(stored.value("updates"_L1).toObject().value("check_interval_minutes"_L1).toInt(), 30);
    QCOMPARE(stored.value("updates"_L1).toObject().value("extra"_L1).toBool(), true);
    QCOMPARE(stored.value("projects"_L1)
                 .toObject()
                 .value("kers-overlay"_L1)
                 .toObject()
                 .value("update_mode"_L1)
                 .toString(),
        u"automatic"_s);
}

void SettingsTests::saveFailureIsReported()
{
    // A regular file where the settings folder should be makes every save fail.
    const QString blocker = m_dir->filePath(u"blocked"_s);
    QVERIFY(writeFile(blocker, "x"));

    SettingsManager settings(blocker + u"/settings.json"_s);
    QSignalSpy failed(&settings, &SettingsManager::saveFailed);

    QCOMPARE(settings.load(), SettingsManager::LoadResult::CreatedDefaults);
    QCOMPARE(failed.count(), 1);

    QVERIFY(!settings.setRegistryAutoCheck(false));
    QCOMPARE(failed.count(), 2);
    QVERIFY(!failed.at(1).at(0).toString().isEmpty());

    // The choice still applies for the running session.
    QVERIFY(!settings.settings().registryAutoCheck);
}

void SettingsTests::noTemporaryFilesAreLeftBehind()
{
    SettingsManager settings(settingsPath());
    settings.load();
    for (int minutes = 15; minutes <= 300; minutes += 15)
        QVERIFY(settings.setUpdateCheckIntervalMinutes(minutes));

    const QStringList files = QDir(m_dir->filePath(u"launcher"_s)).entryList(QDir::Files | QDir::Hidden);
    QCOMPARE(files, QStringList {u"settings.json"_s});
}

QTEST_GUILESS_MAIN(SettingsTests)
#include "SettingsTests.moc"
