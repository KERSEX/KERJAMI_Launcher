// Registry parser: the rules every entry of the registry must pass. The
// registry is the only source of projects, so each rule here guards something:
// what may be shown, what may be installed and where files may end up.

#include "github/RegistryParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;
using launcher::Project;
using launcher::RegistryParseResult;
namespace RP = launcher::RegistryParser;
using Status = launcher::RegistryParseResult::Status;

namespace {

QJsonObject entry(const QString& id, const QString& directory)
{
    return {
        {u"id"_s, id},
        {u"name"_s, u"Project "_s + id},
        {u"description"_s, u"What "_s + id + u" does."_s},
        {u"owner"_s, u"KERSEX"_s},
        {u"repository"_s, u"Repo_"_s + id},
        {u"category"_s, u"Tools"_s},
        {u"enabled"_s, true},
        {u"asset_pattern"_s, id + u"-Windows-x64.zip"_s},
        {u"executable"_s, id + u".exe"_s},
        {u"install_directory"_s, directory},
    };
}

QJsonObject entry(const QString& id)
{
    return entry(id, id);
}

QByteArray document(const QJsonArray& projects, int version = 1)
{
    const QJsonObject root {
        {u"schema_version"_s, version},
        {u"registry_name"_s, u"Test Registry"_s},
        {u"projects"_s, projects},
    };
    return QJsonDocument(root).toJson();
}

/// A JSON value from its text; an empty text stands for "leave the key out".
QJsonValue jsonValue(const QString& text)
{
    if (text.isEmpty())
        return QJsonValue::Undefined;
    return QJsonDocument::fromJson("[" + text.toUtf8() + "]").array().at(0);
}

QString quoted(const QString& text)
{
    return u"\"%1\""_s.arg(text);
}

void row(const char* name, const QString& key, const QString& json)
{
    QTest::newRow(name) << key << json;
}

} // namespace

class RegistryTests : public QObject {
    Q_OBJECT

private slots:
    void parsesAFullEntry();
    void optionalFieldsHaveDefaults();
    void unknownFieldsAreIgnored();
    void displayTextIsTrimmed();
    void tagsAreDeduplicated();
    void projectsKeepTheOrderOfTheFile();
    void disabledProjectsAreCountedButNotListed();
    void bytesAndObjectGiveTheSameResult();

    void unusableDocuments_data();
    void unusableDocuments();
    void projectLimit();

    void invalidEntryIsDroppedAndOthersSurvive_data();
    void invalidEntryIsDroppedAndOthersSurvive();
    void entriesThatAreNotObjectsAreDropped();

    void clashingIdsDropAllInvolvedEntries();
    void clashingInstallDirectoriesDropAllInvolvedEntries();
    void clashWithADisabledEntryCounts();
    void unrelatedEntriesAreNotAffectedByAClash();

    void projectIds_data();
    void projectIds();
    void owners_data();
    void owners();
    void repositories_data();
    void repositories();
    void installDirectories_data();
    void installDirectories();
    void assetPatterns_data();
    void assetPatterns();
    void tagPrefixes_data();
    void tagPrefixes();
    void executablePaths_data();
    void executablePaths();
};

void RegistryTests::parsesAFullEntry()
{
    QJsonObject full = entry(u"kers-overlay"_s, u"KERS_Overlay"_s);
    full.insert(u"name"_s, u"KERS Overlay"_s);
    full.insert(u"description"_s, u"Overlay für F1 und Sim Racing."_s);
    full.insert(u"repository"_s, u"KERS_Overlay"_s);
    full.insert(u"tags"_s, QJsonArray {u"F1"_s, u"Overlay"_s, u"Sim Racing"_s});
    full.insert(u"featured"_s, true);
    full.insert(u"release_channel"_s, u"stable"_s);
    full.insert(u"asset_pattern"_s, u"KERS_Overlay-Windows-x64.zip"_s);
    full.insert(u"tag_prefix"_s, u"KERS_SubsystemsV"_s);
    full.insert(u"executable"_s, u"bin/KERS_Overlay.exe"_s);

    const RegistryParseResult result = RP::parse(document({full}));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.schemaVersion, 1);
    QCOMPARE(result.registryName, u"Test Registry"_s);
    QCOMPARE(result.projects.size(), 1);
    QVERIFY(result.issues.isEmpty());

    const Project& project = result.projects.first();
    QCOMPARE(project.id, u"kers-overlay"_s);
    QCOMPARE(project.name, u"KERS Overlay"_s);
    QCOMPARE(project.description, u"Overlay für F1 und Sim Racing."_s);
    QCOMPARE(project.owner, u"KERSEX"_s);
    QCOMPARE(project.repository, u"KERS_Overlay"_s);
    QCOMPARE(project.category, u"Tools"_s);
    QCOMPARE(project.tags, (QStringList {u"F1"_s, u"Overlay"_s, u"Sim Racing"_s}));
    QVERIFY(project.enabled);
    QVERIFY(project.featured);
    QCOMPARE(project.releaseChannel, launcher::ReleaseChannel::Stable);
    QCOMPARE(project.assetPattern, u"KERS_Overlay-Windows-x64.zip"_s);
    QCOMPARE(project.tagPrefix, u"KERS_SubsystemsV"_s);
    QCOMPARE(project.executable, u"bin/KERS_Overlay.exe"_s);
    QCOMPARE(project.installDirectory, u"KERS_Overlay"_s);
}

void RegistryTests::optionalFieldsHaveDefaults()
{
    const RegistryParseResult result = RP::parse(document({entry(u"alpha"_s)}));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.projects.size(), 1);
    QVERIFY(result.projects.first().tags.isEmpty());
    QVERIFY(!result.projects.first().featured);
    QCOMPARE(result.projects.first().releaseChannel, launcher::ReleaseChannel::Stable);
    QVERIFY(result.projects.first().tagPrefix.isEmpty()); // the tag is the version
}

void RegistryTests::unknownFieldsAreIgnored()
{
    // The registry may grow new fields; an older launcher must keep working.
    QJsonObject extended = entry(u"alpha"_s);
    extended.insert(u"homepage"_s, u"https://example.org"_s);
    extended.insert(u"rating"_s, 5);
    QJsonObject root = QJsonDocument::fromJson(document({extended})).object();
    root.insert(u"maintainers"_s, QJsonArray {u"someone"_s});

    const RegistryParseResult result = RP::parse(root);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.projects.size(), 1);
    QVERIFY(result.issues.isEmpty());
}

void RegistryTests::displayTextIsTrimmed()
{
    QJsonObject padded = entry(u"alpha"_s);
    padded.insert(u"name"_s, u"  Alpha \t"_s);
    padded.insert(u"category"_s, u" Tools "_s);

    const RegistryParseResult result = RP::parse(document({padded}));
    QCOMPARE(result.projects.size(), 1);
    QCOMPARE(result.projects.first().name, u"Alpha"_s);
    QCOMPARE(result.projects.first().category, u"Tools"_s);
}

void RegistryTests::tagsAreDeduplicated()
{
    QJsonObject tagged = entry(u"alpha"_s);
    tagged.insert(u"tags"_s, QJsonArray {u"F1"_s, u"f1"_s, u" F1 "_s, u"Overlay"_s});

    const RegistryParseResult result = RP::parse(document({tagged}));
    QCOMPARE(result.projects.size(), 1);
    QCOMPARE(result.projects.first().tags, (QStringList {u"F1"_s, u"Overlay"_s}));
}

void RegistryTests::projectsKeepTheOrderOfTheFile()
{
    const RegistryParseResult result =
        RP::parse(document({entry(u"charlie"_s), entry(u"alpha"_s), entry(u"bravo"_s)}));
    QCOMPARE(result.projects.size(), 3);
    QCOMPARE(result.projects.at(0).id, u"charlie"_s);
    QCOMPARE(result.projects.at(1).id, u"alpha"_s);
    QCOMPARE(result.projects.at(2).id, u"bravo"_s);
}

void RegistryTests::disabledProjectsAreCountedButNotListed()
{
    QJsonObject hidden = entry(u"hidden"_s);
    hidden.insert(u"enabled"_s, false);

    const RegistryParseResult result = RP::parse(document({entry(u"alpha"_s), hidden, entry(u"bravo"_s)}));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.projects.size(), 2);
    QCOMPARE(result.disabledCount, 1);
    QVERIFY(result.issues.isEmpty());
    for (const Project& project : result.projects)
        QVERIFY(project.id != u"hidden"_s);
}

void RegistryTests::bytesAndObjectGiveTheSameResult()
{
    const QByteArray bytes = document({entry(u"alpha"_s), entry(u"bravo"_s)});
    const RegistryParseResult fromBytes = RP::parse(bytes);
    const RegistryParseResult fromObject = RP::parse(QJsonDocument::fromJson(bytes).object());
    QCOMPARE(fromBytes.projects, fromObject.projects);
    QCOMPARE(fromBytes.document, fromObject.document);
}

void RegistryTests::unusableDocuments_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<int>("status");

    const int invalidJson = static_cast<int>(Status::InvalidJson);
    const int invalidStructure = static_cast<int>(Status::InvalidStructure);
    const int unsupported = static_cast<int>(Status::UnsupportedVersion);

    QTest::newRow("empty") << QByteArray() << invalidJson;
    QTest::newRow("not json") << QByteArray("<html>Not Found</html>") << invalidJson;
    QTest::newRow("truncated") << QByteArray(R"({"schema_version": 1, "projects": [)") << invalidJson;
    QTest::newRow("array at top level") << QByteArray("[]") << invalidStructure;
    // JSON documents are objects or arrays; Qt already refuses a bare value.
    QTest::newRow("string at top level") << QByteArray(R"("registry")") << invalidJson;
    QTest::newRow("number at top level") << QByteArray("42") << invalidJson;
    QTest::newRow("no version") << QByteArray(R"({"projects": []})") << invalidStructure;
    QTest::newRow("version as text") << QByteArray(R"({"schema_version": "1", "projects": []})")
                                     << invalidStructure;
    QTest::newRow("version zero") << QByteArray(R"({"schema_version": 0, "projects": []})")
                                  << invalidStructure;
    QTest::newRow("version negative")
        << QByteArray(R"({"schema_version": -1, "projects": []})") << invalidStructure;
    QTest::newRow("version fraction")
        << QByteArray(R"({"schema_version": 1.5, "projects": []})") << invalidStructure;
    QTest::newRow("projects missing") << QByteArray(R"({"schema_version": 1})") << invalidStructure;
    QTest::newRow("projects is an object")
        << QByteArray(R"({"schema_version": 1, "projects": {}})") << invalidStructure;
    QTest::newRow("projects is text")
        << QByteArray(R"({"schema_version": 1, "projects": "none"})") << invalidStructure;
    QTest::newRow("newer version") << QByteArray(R"({"schema_version": 2, "projects": []})") << unsupported;
    QTest::newRow("much newer version")
        << QByteArray(R"({"schema_version": 99, "projects": "new layout"})") << unsupported;
}

void RegistryTests::unusableDocuments()
{
    QFETCH(QByteArray, json);
    QFETCH(int, status);

    const RegistryParseResult result = RP::parse(json);
    QVERIFY(!result.ok());
    QCOMPARE(static_cast<int>(result.status), status);
    QVERIFY(!result.error.isEmpty());
    QVERIFY(result.projects.isEmpty());
}

void RegistryTests::projectLimit()
{
    const auto manyProjects = [](int count) {
        QJsonArray projects;
        for (int i = 0; i < count; ++i)
            projects.append(entry(u"p%1"_s.arg(i)));
        return projects;
    };

    const RegistryParseResult atLimit = RP::parse(document(manyProjects(RP::kMaxProjects)));
    QVERIFY2(atLimit.ok(), qPrintable(atLimit.error));
    QCOMPARE(atLimit.projects.size(), RP::kMaxProjects);

    const RegistryParseResult overLimit = RP::parse(document(manyProjects(RP::kMaxProjects + 1)));
    QCOMPARE(overLimit.status, Status::InvalidStructure);
    QVERIFY(overLimit.projects.isEmpty());
}

void RegistryTests::invalidEntryIsDroppedAndOthersSurvive_data()
{
    QTest::addColumn<QString>("key");
    QTest::addColumn<QString>("json"); // empty: remove the key

    row("id missing", u"id"_s, {});
    row("id uppercase", u"id"_s, quoted(u"Alpha"_s));
    row("id with space", u"id"_s, quoted(u"my app"_s));
    row("id starts with hyphen", u"id"_s, quoted(u"-alpha"_s));
    row("id ends with hyphen", u"id"_s, quoted(u"alpha-"_s));
    row("id double hyphen", u"id"_s, quoted(u"al--pha"_s));
    row("id too long", u"id"_s, quoted(QString(49, u'a')));
    row("id empty", u"id"_s, u"\"\""_s);
    row("id is a number", u"id"_s, u"123"_s);
    row("id is null", u"id"_s, u"null"_s);
    row("id with newline", u"id"_s, uR"("alpha\n")"_s);

    row("name missing", u"name"_s, {});
    row("name empty", u"name"_s, u"\"\""_s);
    row("name only blanks", u"name"_s, quoted(u"   "_s));
    row("name too long", u"name"_s, quoted(QString(81, u'n')));
    row("name with control character", u"name"_s, uR"("a\u0007b")"_s);
    row("name with direction override", u"name"_s, uR"("exe\u202Egpj.txt")"_s);
    row("name is a number", u"name"_s, u"5"_s);

    row("description missing", u"description"_s, {});
    row("description empty", u"description"_s, u"\"\""_s);
    row("description too long", u"description"_s, quoted(QString(401, u'd')));

    row("owner missing", u"owner"_s, {});
    row("owner with space", u"owner"_s, quoted(u"bad owner"_s));
    row("owner starts with hyphen", u"owner"_s, quoted(u"-bad"_s));
    row("owner ends with hyphen", u"owner"_s, quoted(u"bad-"_s));
    row("owner too long", u"owner"_s, quoted(QString(40, u'o')));
    row("owner with slash", u"owner"_s, quoted(u"a/b"_s));
    row("owner is a number", u"owner"_s, u"5"_s);

    row("repository missing", u"repository"_s, {});
    row("repository dot", u"repository"_s, quoted(u"."_s));
    row("repository dot dot", u"repository"_s, quoted(u".."_s));
    row("repository with slash", u"repository"_s, quoted(u"a/b"_s));
    row("repository ends in .git", u"repository"_s, quoted(u"name.git"_s));
    row("repository empty", u"repository"_s, u"\"\""_s);
    row("repository with space", u"repository"_s, quoted(u"a b"_s));

    row("category missing", u"category"_s, {});
    row("category empty", u"category"_s, u"\"\""_s);
    row("category too long", u"category"_s, quoted(QString(33, u'c')));

    row("tags not a list", u"tags"_s, quoted(u"tools"_s));
    row("too many tags", u"tags"_s, u"[\"1\",\"2\",\"3\",\"4\",\"5\",\"6\",\"7\",\"8\",\"9\"]"_s);
    row("tag is a number", u"tags"_s, u"[\"ok\", 5]"_s);
    row("tag is empty", u"tags"_s, u"[\"\", \"a\"]"_s);
    row("tag too long", u"tags"_s, u"[%1]"_s.arg(quoted(QString(25, u't'))));

    row("enabled missing", u"enabled"_s, {});
    row("enabled is text", u"enabled"_s, quoted(u"yes"_s));
    row("enabled is a number", u"enabled"_s, u"1"_s);
    row("featured is text", u"featured"_s, quoted(u"yes"_s));

    row("release channel unknown", u"release_channel"_s, quoted(u"beta"_s));
    row("release channel is a number", u"release_channel"_s, u"1"_s);

    row("asset pattern missing", u"asset_pattern"_s, {});
    row("asset pattern is not a zip", u"asset_pattern"_s, quoted(u"app.exe"_s));
    row("asset pattern with parent folder", u"asset_pattern"_s, quoted(u"../app.zip"_s));
    row("asset pattern with slash", u"asset_pattern"_s, quoted(u"a/b.zip"_s));
    row("asset pattern with backslash", u"asset_pattern"_s, uR"("a\\b.zip")"_s);
    row("asset pattern with question mark", u"asset_pattern"_s, quoted(u"app?.zip"_s));
    row("asset pattern with colon", u"asset_pattern"_s, quoted(u"c:app.zip"_s));
    row("asset pattern empty", u"asset_pattern"_s, u"\"\""_s);

    row("tag prefix empty", u"tag_prefix"_s, u"\"\""_s);
    row("tag prefix starts with a digit", u"tag_prefix"_s, quoted(u"1app-v"_s));
    row("tag prefix starts with a hyphen", u"tag_prefix"_s, quoted(u"-v"_s));
    row("tag prefix with slash", u"tag_prefix"_s, quoted(u"release/v"_s));
    row("tag prefix with space", u"tag_prefix"_s, quoted(u"My App v"_s));
    row("tag prefix with backslash", u"tag_prefix"_s, uR"("a\\b")"_s);
    row("tag prefix too long", u"tag_prefix"_s, quoted(QString(33, u'p')));
    row("tag prefix is a number", u"tag_prefix"_s, u"5"_s);
    row("tag prefix is null", u"tag_prefix"_s, u"null"_s);

    row("executable missing", u"executable"_s, {});
    row("executable absolute", u"executable"_s, quoted(u"/app.exe"_s));
    row("executable with drive", u"executable"_s, quoted(u"C:/app.exe"_s));
    row("executable with backslash", u"executable"_s, uR"("bin\\app.exe")"_s);
    row("executable above the folder", u"executable"_s, quoted(u"../app.exe"_s));
    row("executable hidden traversal", u"executable"_s, quoted(u"bin/../../app.exe"_s));
    row("executable empty segment", u"executable"_s, quoted(u"bin//app.exe"_s));
    row("executable is not an exe", u"executable"_s, quoted(u"app.bat"_s));
    row("executable device name", u"executable"_s, quoted(u"con.exe"_s));
    row("executable stream", u"executable"_s, quoted(u"app.exe:hidden"_s));
    row("executable with wildcard", u"executable"_s, quoted(u"bin/*.exe"_s));
    row("executable empty", u"executable"_s, u"\"\""_s);

    row("directory missing", u"install_directory"_s, {});
    row("directory dot dot", u"install_directory"_s, quoted(u".."_s));
    row("directory dot", u"install_directory"_s, quoted(u"."_s));
    row("directory with slash", u"install_directory"_s, quoted(u"a/b"_s));
    row("directory with backslash", u"install_directory"_s, uR"("a\\b")"_s);
    row("directory device name", u"install_directory"_s, quoted(u"CON"_s));
    row("directory device name with extension", u"install_directory"_s, quoted(u"nul.txt"_s));
    row("directory numbered device", u"install_directory"_s, quoted(u"COM1"_s));
    row("directory trailing dot", u"install_directory"_s, quoted(u"app."_s));
    row("directory hidden", u"install_directory"_s, quoted(u".app"_s));
    row("directory too long", u"install_directory"_s, quoted(QString(65, u'd')));
    row("directory with space", u"install_directory"_s, quoted(u"my app"_s));
    row("directory with drive", u"install_directory"_s, quoted(u"C:"_s));
    row("directory with umlaut", u"install_directory"_s, quoted(u"Übung"_s));
    row("directory empty", u"install_directory"_s, u"\"\""_s);

    // The launcher keeps this id for its own updates; a project with it would share their folder.
    row("id reserved for the launcher", u"id"_s, quoted(u"launcher"_s));
}

void RegistryTests::invalidEntryIsDroppedAndOthersSurvive()
{
    QFETCH(QString, key);
    QFETCH(QString, json);

    QJsonObject broken = entry(u"broken"_s);
    const QJsonValue value = jsonValue(json);
    if (value.isUndefined())
        broken.remove(key);
    else
        broken.insert(key, value);

    const RegistryParseResult result = RP::parse(document({entry(u"good"_s), broken}));

    // One bad entry never takes the document or its neighbours down with it.
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.projects.size(), 1);
    QCOMPARE(result.projects.first().id, u"good"_s);
    QCOMPARE(result.issues.size(), 1);
    QCOMPARE(result.issues.first().index, 1);
    QVERIFY2(result.issues.first().message.contains(quoted(key)), qPrintable(result.issues.first().message));
}

void RegistryTests::entriesThatAreNotObjectsAreDropped()
{
    const RegistryParseResult result = RP::parse(document({entry(u"alpha"_s), 5, u"text"_s, QJsonValue::Null,
        QJsonArray {entry(u"nested"_s)}, entry(u"bravo"_s)}));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.projects.size(), 2);
    QCOMPARE(result.issues.size(), 4);
    QCOMPARE(result.issues.at(0).index, 1);
    QCOMPARE(result.issues.at(3).index, 4);
}

void RegistryTests::clashingIdsDropAllInvolvedEntries()
{
    // Which of two entries with the same id is the real one cannot be told, so none is trusted.
    const RegistryParseResult result = RP::parse(
        document({entry(u"alpha"_s, u"First"_s), entry(u"bravo"_s), entry(u"alpha"_s, u"Second"_s)}));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.projects.size(), 1);
    QCOMPARE(result.projects.first().id, u"bravo"_s);
    QCOMPARE(result.issues.size(), 2);
    QCOMPARE(result.issues.at(0).index, 0);
    QCOMPARE(result.issues.at(1).index, 2);
    QVERIFY(result.issues.at(0).message.contains(u"\"id\""_s));
}

void RegistryTests::clashingInstallDirectoriesDropAllInvolvedEntries()
{
    // Two projects in one folder would overwrite each other's files. Windows
    // ignores case in folder names, so "Tools" and "tools" are the same folder.
    const RegistryParseResult result = RP::parse(
        document({entry(u"alpha"_s, u"Tools"_s), entry(u"bravo"_s, u"tools"_s), entry(u"charlie"_s)}));
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.projects.size(), 1);
    QCOMPARE(result.projects.first().id, u"charlie"_s);
    QCOMPARE(result.issues.size(), 2);
    QVERIFY(result.issues.at(0).message.contains(u"\"install_directory\""_s));
}

void RegistryTests::clashWithADisabledEntryCounts()
{
    QJsonObject disabled = entry(u"alpha"_s, u"Other"_s);
    disabled.insert(u"enabled"_s, false);

    const RegistryParseResult result = RP::parse(document({entry(u"alpha"_s), disabled, entry(u"bravo"_s)}));
    QCOMPARE(result.projects.size(), 1);
    QCOMPARE(result.projects.first().id, u"bravo"_s);
    QCOMPARE(result.issues.size(), 2);
    QCOMPARE(result.disabledCount, 0);
}

void RegistryTests::unrelatedEntriesAreNotAffectedByAClash()
{
    const RegistryParseResult result = RP::parse(
        document({entry(u"alpha"_s), entry(u"alpha"_s, u"Else"_s), entry(u"bravo"_s), entry(u"charlie"_s)}));
    QCOMPARE(result.projects.size(), 2);
    QCOMPARE(result.projects.at(0).id, u"bravo"_s);
    QCOMPARE(result.projects.at(1).id, u"charlie"_s);
}

// ---- Rules for single values, also used by the installer --------------------

void RegistryTests::projectIds_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<bool>("valid");

    QTest::newRow("letters") << u"overlay"_s << true;
    QTest::newRow("digits") << u"p2"_s << true;
    QTest::newRow("hyphens") << u"kers-overlay-2"_s << true;
    QTest::newRow("48 characters") << QString(48, u'a') << true;
    QTest::newRow("single character") << u"a"_s << true;
    QTest::newRow("49 characters") << QString(49, u'a') << false;
    QTest::newRow("uppercase") << u"Overlay"_s << false;
    QTest::newRow("underscore") << u"kers_overlay"_s << false;
    QTest::newRow("space") << u"kers overlay"_s << false;
    QTest::newRow("leading hyphen") << u"-a"_s << false;
    QTest::newRow("trailing hyphen") << u"a-"_s << false;
    QTest::newRow("double hyphen") << u"a--b"_s << false;
    QTest::newRow("trailing newline") << u"a\n"_s << false;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("slash") << u"a/b"_s << false;
}

void RegistryTests::projectIds()
{
    QFETCH(QString, value);
    QFETCH(bool, valid);
    QCOMPARE(RP::isValidProjectId(value), valid);
}

void RegistryTests::owners_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<bool>("valid");

    QTest::newRow("name") << u"KERSEX"_s << true;
    QTest::newRow("with hyphen") << u"my-org"_s << true;
    QTest::newRow("single character") << u"a"_s << true;
    QTest::newRow("39 characters") << QString(39, u'a') << true;
    QTest::newRow("40 characters") << QString(40, u'a') << false;
    QTest::newRow("leading hyphen") << u"-org"_s << false;
    QTest::newRow("trailing hyphen") << u"org-"_s << false;
    QTest::newRow("underscore") << u"my_org"_s << false;
    QTest::newRow("dot") << u"my.org"_s << false;
    QTest::newRow("slash") << u"a/b"_s << false;
    QTest::newRow("trailing newline") << u"org\n"_s << false;
    QTest::newRow("empty") << QString() << false;
}

void RegistryTests::owners()
{
    QFETCH(QString, value);
    QFETCH(bool, valid);
    QCOMPARE(RP::isValidOwner(value), valid);
}

void RegistryTests::repositories_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<bool>("valid");

    QTest::newRow("name") << u"KERS_Overlay"_s << true;
    QTest::newRow("dots and hyphens") << u"my.repo-2"_s << true;
    QTest::newRow("100 characters") << QString(100, u'r') << true;
    QTest::newRow("101 characters") << QString(101, u'r') << false;
    QTest::newRow("dot") << u"."_s << false;
    QTest::newRow("dot dot") << u".."_s << false;
    QTest::newRow("git suffix") << u"repo.git"_s << false;
    QTest::newRow("git suffix uppercase") << u"repo.GIT"_s << false;
    QTest::newRow("slash") << u"a/b"_s << false;
    QTest::newRow("space") << u"a b"_s << false;
    QTest::newRow("empty") << QString() << false;
}

void RegistryTests::repositories()
{
    QFETCH(QString, value);
    QFETCH(bool, valid);
    QCOMPARE(RP::isValidRepository(value), valid);
}

void RegistryTests::installDirectories_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<bool>("valid");

    QTest::newRow("name") << u"KERS_Overlay"_s << true;
    QTest::newRow("dots inside") << u"My.App_1-x"_s << true;
    QTest::newRow("64 characters") << QString(64, u'd') << true;
    QTest::newRow("65 characters") << QString(65, u'd') << false;
    QTest::newRow("device name") << u"CON"_s << false;
    QTest::newRow("device name lowercase") << u"con"_s << false;
    QTest::newRow("device name with extension") << u"aux.log"_s << false;
    QTest::newRow("numbered device") << u"COM3"_s << false;
    QTest::newRow("numbered printer") << u"lpt9"_s << false;
    QTest::newRow("looks like a device") << u"console"_s << true;
    QTest::newRow("device number out of range") << u"COM10"_s << true;
    QTest::newRow("dot") << u"."_s << false;
    QTest::newRow("dot dot") << u".."_s << false;
    QTest::newRow("hidden") << u".app"_s << false;
    QTest::newRow("trailing dot") << u"app."_s << false;
    QTest::newRow("slash") << u"a/b"_s << false;
    QTest::newRow("backslash") << u"a\\b"_s << false;
    QTest::newRow("drive") << u"C:"_s << false;
    QTest::newRow("space") << u"my app"_s << false;
    QTest::newRow("wildcard") << u"app*"_s << false;
    QTest::newRow("umlaut") << u"Übung"_s << false;
    QTest::newRow("trailing newline") << u"app\n"_s << false;
    QTest::newRow("empty") << QString() << false;
}

void RegistryTests::installDirectories()
{
    QFETCH(QString, value);
    QFETCH(bool, valid);
    QCOMPARE(RP::isValidInstallDirectory(value), valid);
}

void RegistryTests::assetPatterns_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<bool>("valid");

    QTest::newRow("file name") << u"KERS_Overlay-Windows-x64.zip"_s << true;
    QTest::newRow("wildcard") << u"Overlay-*-Windows-x64.zip"_s << true;
    QTest::newRow("only a wildcard") << u"*.zip"_s << true;
    QTest::newRow("uppercase extension") << u"APP.ZIP"_s << true;
    QTest::newRow("brackets and plus") << u"app+(1).zip"_s << true;
    QTest::newRow("not a zip") << u"app.exe"_s << false;
    QTest::newRow("tar") << u"app.tar.gz"_s << false;
    QTest::newRow("no extension") << u"app"_s << false;
    QTest::newRow("slash") << u"a/app.zip"_s << false;
    QTest::newRow("backslash") << u"a\\app.zip"_s << false;
    QTest::newRow("parent folder") << u"../app.zip"_s << false;
    QTest::newRow("question mark") << u"app?.zip"_s << false;
    QTest::newRow("colon") << u"c:app.zip"_s << false;
    QTest::newRow("space") << u"my app.zip"_s << false;
    QTest::newRow("129 characters") << QString(125, u'a') + u".zip"_s << false;
    QTest::newRow("trailing newline") << u"app.zip\n"_s << false;
    QTest::newRow("empty") << QString() << false;
}

void RegistryTests::assetPatterns()
{
    QFETCH(QString, value);
    QFETCH(bool, valid);
    QCOMPARE(RP::isValidAssetPattern(value), valid);
}

void RegistryTests::tagPrefixes_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<bool>("valid");

    QTest::newRow("name and V") << u"TankstellenmealV"_s << true;
    QTest::newRow("with underscore") << u"KERS_SubsystemsV"_s << true;
    QTest::newRow("with hyphen") << u"app-v"_s << true;
    QTest::newRow("with dot") << u"release.v"_s << true;
    QTest::newRow("a single letter") << u"v"_s << true;
    QTest::newRow("32 characters") << QString(32, u'p') << true;
    QTest::newRow("33 characters") << QString(33, u'p') << false;
    QTest::newRow("starts with a digit") << u"1v"_s << false;
    QTest::newRow("starts with a dot") << u".v"_s << false;
    QTest::newRow("starts with an underscore") << u"_v"_s << false;
    QTest::newRow("slash") << u"a/v"_s << false;
    QTest::newRow("backslash") << u"a\\v"_s << false;
    QTest::newRow("space") << u"a v"_s << false;
    QTest::newRow("question mark") << u"a?v"_s << false;
    QTest::newRow("percent") << u"a%2Fv"_s << false;
    QTest::newRow("trailing newline") << u"app-v\n"_s << false;
    QTest::newRow("empty") << QString() << false;
}

void RegistryTests::tagPrefixes()
{
    QFETCH(QString, value);
    QFETCH(bool, valid);
    QCOMPARE(RP::isValidTagPrefix(value), valid);
}

void RegistryTests::executablePaths_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<bool>("valid");

    QTest::newRow("file") << u"App.exe"_s << true;
    QTest::newRow("uppercase extension") << u"App.EXE"_s << true;
    QTest::newRow("in a folder") << u"bin/App.exe"_s << true;
    QTest::newRow("deeper") << u"app/bin/x64/App.exe"_s << true;
    QTest::newRow("spaces and dots") << u"My App/App v1.2.exe"_s << true;
    QTest::newRow("absolute") << u"/App.exe"_s << false;
    QTest::newRow("drive") << u"C:/App.exe"_s << false;
    QTest::newRow("drive relative") << u"C:App.exe"_s << false;
    QTest::newRow("backslash") << u"bin\\App.exe"_s << false;
    QTest::newRow("unc") << u"//server/share/App.exe"_s << false;
    QTest::newRow("parent") << u"../App.exe"_s << false;
    QTest::newRow("parent inside") << u"bin/../../App.exe"_s << false;
    QTest::newRow("current folder") << u"./App.exe"_s << false;
    QTest::newRow("empty segment") << u"bin//App.exe"_s << false;
    QTest::newRow("not an exe") << u"App.bat"_s << false;
    QTest::newRow("ends in a folder") << u"bin/"_s << false;
    QTest::newRow("data stream") << u"App.exe:hidden"_s << false;
    QTest::newRow("device name") << u"CON.exe"_s << false;
    QTest::newRow("device folder") << u"nul/App.exe"_s << false;
    QTest::newRow("trailing dot in folder") << u"bin./App.exe"_s << false;
    QTest::newRow("trailing space in folder") << u"bin /App.exe"_s << false;
    QTest::newRow("wildcard") << u"bin/*.exe"_s << false;
    QTest::newRow("pipe") << u"bin/a|b.exe"_s << false;
    QTest::newRow("control character") << u"bin/a\u0001b.exe"_s << false;
    QTest::newRow("201 characters") << QString(197, u'a') + u".exe"_s << false;
    QTest::newRow("empty") << QString() << false;
}

void RegistryTests::executablePaths()
{
    QFETCH(QString, value);
    QFETCH(bool, valid);
    QCOMPARE(RP::isValidExecutablePath(value), valid);
}

QTEST_GUILESS_MAIN(RegistryTests)
#include "RegistryTests.moc"
