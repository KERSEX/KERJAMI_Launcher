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
using launcher::UpdateMode;

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

    void projectsAreManualUntilTheUserChoosesOtherwise();
    void projectModeIsPersistedImmediately();
    void projectModesAreIndependentOfEachOther();
    void unchangedProjectModeDoesNotWriteOrNotify();
    void invalidProjectIdsAreRefused();
    void removingProjectSettingsForgetsThem();
    void damagedProjectSettingsNeverSwitchUpdatesOn();
    void projectSettingsKeepForeignKeysAndForeignEntries();
    void projectSettingsAreNotTiedToAnythingElse();

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

// ---- Settings of single projects -----------------------------------------------------

void SettingsTests::projectsAreManualUntilTheUserChoosesOtherwise()
{
    SettingsManager manager(settingsPath());
    manager.load();
    // Every project, known or not, starts as manual: nothing is updated behind the user's back.
    QCOMPARE(manager.updateMode(u"kers-overlay"_s), UpdateMode::Manual);
    QCOMPARE(manager.updateMode(u"never-heard-of"_s), UpdateMode::Manual);
    QVERIFY(!readJson(settingsPath()).contains("projects"_L1));
}

void SettingsTests::projectModeIsPersistedImmediately()
{
    SettingsManager manager(settingsPath());
    manager.load();
    QSignalSpy project(&manager, &SettingsManager::projectSettingsChanged);
    QSignalSpy general(&manager, &SettingsManager::settingsChanged);

    QVERIFY(manager.setUpdateMode(u"kers-overlay"_s, UpdateMode::Automatic));
    QCOMPARE(manager.updateMode(u"kers-overlay"_s), UpdateMode::Automatic);
    QCOMPARE(project.size(), 1);
    QCOMPARE(project.first().first().toString(), u"kers-overlay"_s);
    QCOMPARE(general.size(), 1);

    // On disk before the call returned, in the shape of the specification.
    const auto storedMode = [this](const QString& id) {
        return readJson(settingsPath())
            .value("projects"_L1)
            .toObject()
            .value(id)
            .toObject()
            .value("update_mode"_L1)
            .toString();
    };
    QCOMPARE(storedMode(u"kers-overlay"_s), u"automatic"_s);

    // And back after a restart.
    SettingsManager again(settingsPath());
    again.load();
    QCOMPARE(again.updateMode(u"kers-overlay"_s), UpdateMode::Automatic);

    QVERIFY(manager.setUpdateMode(u"kers-overlay"_s, UpdateMode::Manual));
    QCOMPARE(storedMode(u"kers-overlay"_s), u"manual"_s);
}

void SettingsTests::projectModesAreIndependentOfEachOther()
{
    SettingsManager manager(settingsPath());
    manager.load();
    QVERIFY(manager.setUpdateMode(u"alpha"_s, UpdateMode::Automatic));
    QVERIFY(manager.setUpdateMode(u"beta"_s, UpdateMode::Manual));
    QVERIFY(manager.setUpdateMode(u"gamma"_s, UpdateMode::Automatic));
    QVERIFY(manager.setUpdateMode(u"gamma"_s, UpdateMode::Manual));

    SettingsManager again(settingsPath());
    again.load();
    QCOMPARE(again.updateMode(u"alpha"_s), UpdateMode::Automatic);
    QCOMPARE(again.updateMode(u"beta"_s), UpdateMode::Manual);
    QCOMPARE(again.updateMode(u"gamma"_s), UpdateMode::Manual);
}

void SettingsTests::unchangedProjectModeDoesNotWriteOrNotify()
{
    SettingsManager manager(settingsPath());
    manager.load();
    QVERIFY(manager.setUpdateMode(u"alpha"_s, UpdateMode::Automatic));
    const QDateTime stamp = QFileInfo(settingsPath()).lastModified();
    QTest::qWait(30);

    QSignalSpy project(&manager, &SettingsManager::projectSettingsChanged);
    QVERIFY(manager.setUpdateMode(u"alpha"_s, UpdateMode::Automatic));
    QCOMPARE(project.size(), 0);
    QCOMPARE(QFileInfo(settingsPath()).lastModified(), stamp);
}

void SettingsTests::invalidProjectIdsAreRefused()
{
    SettingsManager manager(settingsPath());
    manager.load();
    for (const QString& id : {QString(), u"../x"_s, u"Upper"_s, u"a b"_s, u"x"_s.repeated(60)}) {
        QVERIFY2(!manager.setUpdateMode(id, UpdateMode::Automatic), qPrintable(id));
        QCOMPARE(manager.updateMode(id), UpdateMode::Manual);
    }
    QVERIFY(!readJson(settingsPath()).contains("projects"_L1));
}

void SettingsTests::removingProjectSettingsForgetsThem()
{
    SettingsManager manager(settingsPath());
    manager.load();
    QVERIFY(manager.setUpdateMode(u"alpha"_s, UpdateMode::Automatic));
    QVERIFY(manager.setUpdateMode(u"beta"_s, UpdateMode::Automatic));

    QSignalSpy project(&manager, &SettingsManager::projectSettingsChanged);
    manager.removeProjectSettings(u"alpha"_s);
    manager.removeProjectSettings(u"alpha"_s); // already gone: nothing happens
    manager.removeProjectSettings(u"never-heard-of"_s);
    QCOMPARE(project.size(), 1);

    QCOMPARE(manager.updateMode(u"alpha"_s), UpdateMode::Manual);
    QCOMPARE(manager.updateMode(u"beta"_s), UpdateMode::Automatic);
    const QJsonObject projects = readJson(settingsPath()).value("projects"_L1).toObject();
    QVERIFY(!projects.contains("alpha"_L1));
    QVERIFY(projects.contains("beta"_L1));

    manager.removeProjectSettings(u"beta"_s);
    QVERIFY(!readJson(settingsPath()).contains("projects"_L1)); // an empty section is not kept
}

void SettingsTests::damagedProjectSettingsNeverSwitchUpdatesOn()
{
    QVERIFY(writeFile(settingsPath(), R"({"schema_version":1,"projects":{
        "alpha":{"update_mode":"automatic"},
        "beta":{"update_mode":"sometimes"},
        "gamma":{"update_mode":5},
        "delta":"automatic",
        "epsilon":{},
        "Bad Id":{"update_mode":"automatic"}}})"));
    SettingsManager manager(settingsPath());
    manager.load();
    QCOMPARE(manager.updateMode(u"alpha"_s), UpdateMode::Automatic);
    QCOMPARE(manager.updateMode(u"beta"_s), UpdateMode::Manual);
    QCOMPARE(manager.updateMode(u"gamma"_s), UpdateMode::Manual);
    QCOMPARE(manager.updateMode(u"delta"_s), UpdateMode::Manual);
    QCOMPARE(manager.updateMode(u"epsilon"_s), UpdateMode::Manual);
    QCOMPARE(manager.updateMode(u"Bad Id"_s), UpdateMode::Manual);

    // "projects" that is not an object at all.
    QVERIFY(writeFile(settingsPath(), R"({"schema_version":1,"projects":["alpha"]})"));
    SettingsManager other(settingsPath());
    other.load();
    QCOMPARE(other.updateMode(u"alpha"_s), UpdateMode::Manual);
}

void SettingsTests::projectSettingsKeepForeignKeysAndForeignEntries()
{
    QVERIFY(writeFile(settingsPath(), R"({"schema_version":1,"projects":{
        "alpha":{"update_mode":"manual","note":"kept"},
        "Weird Entry":{"x":1}}})"));
    SettingsManager manager(settingsPath());
    manager.load();
    QVERIFY(manager.setUpdateMode(u"alpha"_s, UpdateMode::Automatic));
    QVERIFY(manager.setUpdateMode(u"beta"_s, UpdateMode::Automatic));
    manager.removeProjectSettings(u"beta"_s);

    const QJsonObject projects = readJson(settingsPath()).value("projects"_L1).toObject();
    QCOMPARE(projects.value("alpha"_L1).toObject().value("update_mode"_L1).toString(), u"automatic"_s);
    // A key this build does not know, and an entry with an id it does not accept, stay as they were.
    QCOMPARE(projects.value("alpha"_L1).toObject().value("note"_L1).toString(), u"kept"_s);
    QVERIFY(projects.contains("Weird Entry"_L1));
    QVERIFY(!projects.contains("beta"_L1));
}

void SettingsTests::projectSettingsAreNotTiedToAnythingElse()
{
    // The mode belongs to the project id alone: it does not change when the
    // general settings do, and the general settings do not change when it does.
    SettingsManager manager(settingsPath());
    manager.load();
    QVERIFY(manager.setUpdateMode(u"alpha"_s, UpdateMode::Automatic));
    QVERIFY(manager.setStartPage(StartPage::Library));
    QVERIFY(manager.setUpdateCheckIntervalMinutes(30));
    QCOMPARE(manager.updateMode(u"alpha"_s), UpdateMode::Automatic);

    QVERIFY(manager.setUpdateMode(u"alpha"_s, UpdateMode::Manual));
    QCOMPARE(manager.settings().startPage, StartPage::Library);
    QCOMPARE(manager.settings().updateCheckIntervalMinutes, 30);

    SettingsManager again(settingsPath());
    again.load();
    QCOMPARE(again.settings().startPage, StartPage::Library);
    QCOMPARE(again.updateMode(u"alpha"_s), UpdateMode::Manual);
}

QTEST_GUILESS_MAIN(SettingsTests)
#include "SettingsTests.moc"
