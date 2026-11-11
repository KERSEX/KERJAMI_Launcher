// The list of approved projects, which is part of the program: the file that is
// compiled in must be valid, and the manager must keep a good list when it is
// given a bad document.

#include "github/RegistryManager.h"
#include "github/RegistryParser.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using launcher::RegistryManager;

namespace {

QJsonObject entry(const QString& id, bool enabled = true)
{
    return {
        {u"id"_s, id},
        {u"name"_s, u"Project "_s + id},
        {u"description"_s, u"What "_s + id + u" does."_s},
        {u"owner"_s, u"KERSEX"_s},
        {u"repository"_s, u"Repo_"_s + id},
        {u"category"_s, u"Tools"_s},
        {u"enabled"_s, enabled},
        {u"asset_pattern"_s, id + u"-*.zip"_s},
        {u"executable"_s, id + u".exe"_s},
        {u"install_directory"_s, id},
    };
}

QByteArray document(const QJsonArray& projects, int schema = 1)
{
    return QJsonDocument(QJsonObject {{u"schema_version"_s, schema}, {u"registry_name"_s, u"Test"_s},
                             {u"projects"_s, projects}})
        .toJson();
}

} // namespace

class RegistryManagerTests : public QObject {
    Q_OBJECT

private slots:
    // === the list that is compiled in ======================================================

    void theBuiltInListIsInTheProgramAndValid()
    {
        QVERIFY2(QFile::exists(RegistryManager::builtInResource()), "the list is not compiled in");

        RegistryManager manager;
        QVERIFY2(manager.loadBuiltIn(), qPrintable(manager.error()));
        QVERIFY(manager.error().isEmpty());
        // An entry with a mistake is dropped silently at run time; here it fails the build.
        for (const launcher::RegistryIssue& issue : manager.issues())
            QFAIL(qPrintable(u"entry %1 (%2): %3"_s.arg(issue.index).arg(issue.projectId, issue.message)));
        QVERIFY(manager.issues().isEmpty());
        QVERIFY(!manager.registryName().isEmpty());
    }

    void theBuiltInProjectsAreUsable()
    {
        RegistryManager manager;
        QVERIFY(manager.loadBuiltIn());

        // Every entry passes the same checks the launcher applies before it touches a project.
        for (const launcher::Project& project : manager.projects()) {
            const QString name = project.id;
            QVERIFY2(launcher::RegistryParser::isValidProjectId(project.id), qPrintable(name));
            QVERIFY2(!launcher::RegistryParser::isReservedProjectId(project.id), qPrintable(name));
            QVERIFY2(launcher::RegistryParser::isValidOwner(project.owner), qPrintable(name));
            QVERIFY2(launcher::RegistryParser::isValidRepository(project.repository), qPrintable(name));
            QVERIFY2(launcher::RegistryParser::isValidAssetPattern(project.assetPattern), qPrintable(name));
            QVERIFY2(launcher::RegistryParser::isValidExecutablePath(project.executable), qPrintable(name));
            QVERIFY2(launcher::RegistryParser::isValidInstallDirectory(project.installDirectory),
                qPrintable(name));
            for (const QString& kept : project.keep)
                QVERIFY2(launcher::RegistryParser::isValidKeepPath(kept), qPrintable(name + u": "_s + kept));
            if (!project.tagPrefix.isEmpty())
                QVERIFY2(launcher::RegistryParser::isValidTagPrefix(project.tagPrefix), qPrintable(name));
            QVERIFY2(project.enabled, qPrintable(name)); // disabled entries are not listed at all
        }
    }

    // === loading a document ===================================================================

    void loadsAValidDocument()
    {
        RegistryManager manager;
        QSignalSpy changed(&manager, &RegistryManager::changed);

        QVERIFY(manager.projects().isEmpty());
        QVERIFY(manager.load(document({entry(u"alpha"_s), entry(u"beta"_s)})));
        QCOMPARE(manager.projects().size(), 2);
        QCOMPARE(manager.projects().at(0).id, u"alpha"_s); // the order of the file
        QCOMPARE(manager.projects().at(1).id, u"beta"_s);
        QCOMPARE(manager.registryName(), u"Test"_s);
        QVERIFY(manager.error().isEmpty());
        QCOMPARE(changed.size(), 1);
    }

    void disabledProjectsAreNotListed()
    {
        RegistryManager manager;
        QVERIFY(manager.load(document({entry(u"alpha"_s), entry(u"hidden"_s, false)})));
        QCOMPARE(manager.projects().size(), 1);
        QCOMPARE(manager.projects().first().id, u"alpha"_s);
    }

    void aRejectedEntryCostsOnlyThatEntry()
    {
        QJsonObject broken = entry(u"broken"_s);
        broken.insert(u"asset_pattern"_s, u"not-a-zip.msi"_s);

        RegistryManager manager;
        QVERIFY(manager.load(document({entry(u"alpha"_s), broken, entry(u"beta"_s)})));
        QCOMPARE(manager.projects().size(), 2);
        QCOMPARE(manager.issues().size(), 1);
        QCOMPARE(manager.issues().first().projectId, u"broken"_s);
    }

    void loadingAgainReplacesTheList()
    {
        RegistryManager manager;
        QVERIFY(manager.load(document({entry(u"alpha"_s)})));
        QSignalSpy changed(&manager, &RegistryManager::changed);
        QVERIFY(manager.load(document({entry(u"beta"_s), entry(u"gamma"_s)})));
        QCOMPARE(manager.projects().size(), 2);
        QCOMPARE(manager.projects().first().id, u"beta"_s);
        QCOMPARE(changed.size(), 1);
    }

    // === a document that cannot be used ==========================================================

    void anUnusableDocumentKeepsTheList_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("not json") << QByteArray("{ nope");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("an array") << QByteArray("[1,2,3]");
        QTest::newRow("schema from the future") << document({entry(u"x"_s)}, 2);
        QTest::newRow("no schema") << QByteArray(R"({"projects": []})");
        QTest::newRow("projects is not a list") << QByteArray(R"({"schema_version":1,"projects":"x"})");
    }

    void anUnusableDocumentKeepsTheList()
    {
        QFETCH(QByteArray, json);

        RegistryManager manager;
        QVERIFY(manager.load(document({entry(u"alpha"_s)})));

        QSignalSpy changed(&manager, &RegistryManager::changed);
        QVERIFY(!manager.load(json));
        QVERIFY(!manager.error().isEmpty());
        QCOMPARE(manager.projects().size(), 1);
        QCOMPARE(manager.projects().first().id, u"alpha"_s);
        QCOMPARE(changed.size(), 0);

        // A good document clears the error again.
        QVERIFY(manager.load(document({entry(u"beta"_s)})));
        QVERIFY(manager.error().isEmpty());
    }
};

QTEST_GUILESS_MAIN(RegistryManagerTests)

#include "RegistryManagerTests.moc"
