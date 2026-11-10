// Semantic Versioning: which texts are versions and how versions are ordered.
// The ordering examples come from the specification (semver.org, item 11).

#include "core/SemVer.h"

#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;
using launcher::SemVer;

namespace {

SemVer version(const QString& text)
{
    const std::optional<SemVer> parsed = SemVer::parse(text);
    return parsed.value_or(SemVer {});
}

} // namespace

class SemVerTests : public QObject {
    Q_OBJECT

private slots:
    void validVersions_data();
    void validVersions();
    void invalidVersions_data();
    void invalidVersions();
    void largestAcceptedNumbers();

    void specificationOrder();
    void numbersAreComparedAsNumbers();
    void preReleaseIsBelowRelease();
    void preReleaseIdentifiers();
    void buildMetadataIsIgnored();
    void equalVersionsAreEqual();
    void newestOfAListDoesNotDependOnItsOrder();
    void toStringRoundTrips();
};

void SemVerTests::validVersions_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("major");
    QTest::addColumn<int>("minor");
    QTest::addColumn<int>("patch");
    QTest::addColumn<QStringList>("preRelease");
    QTest::addColumn<QString>("build");

    QTest::newRow("plain") << u"1.2.3"_s << 1 << 2 << 3 << QStringList() << QString();
    QTest::newRow("zero") << u"0.0.0"_s << 0 << 0 << 0 << QStringList() << QString();
    QTest::newRow("tag with v") << u"v1.2.3"_s << 1 << 2 << 3 << QStringList() << QString();
    QTest::newRow("tag with capital V") << u"V10.20.30"_s << 10 << 20 << 30 << QStringList() << QString();
    QTest::newRow("pre-release") << u"1.0.0-alpha"_s << 1 << 0 << 0 << QStringList {u"alpha"_s} << QString();
    QTest::newRow("dotted pre-release")
        << u"1.0.0-alpha.1"_s << 1 << 0 << 0 << QStringList {u"alpha"_s, u"1"_s} << QString();
    QTest::newRow("numeric pre-release")
        << u"1.0.0-0.3.7"_s << 1 << 0 << 0 << QStringList {u"0"_s, u"3"_s, u"7"_s} << QString();
    QTest::newRow("mixed pre-release")
        << u"1.0.0-x.7.z.92"_s << 1 << 0 << 0 << QStringList {u"x"_s, u"7"_s, u"z"_s, u"92"_s} << QString();
    QTest::newRow("hyphens in pre-release")
        << u"1.0.0-x-y-z.--"_s << 1 << 0 << 0 << QStringList {u"x-y-z"_s, u"--"_s} << QString();
    QTest::newRow("build metadata") << u"1.0.0+20130313144700"_s << 1 << 0 << 0 << QStringList()
                                    << u"20130313144700"_s;
    QTest::newRow("pre-release and build")
        << u"1.0.0-beta+exp.sha.5114f85"_s << 1 << 0 << 0 << QStringList {u"beta"_s} << u"exp.sha.5114f85"_s;
    QTest::newRow("digit and letter identifier")
        << u"1.0.0-1a"_s << 1 << 0 << 0 << QStringList {u"1a"_s} << QString();
    QTest::newRow("build with leading zeros") << u"1.0.0+001"_s << 1 << 0 << 0 << QStringList() << u"001"_s;
}

void SemVerTests::validVersions()
{
    QFETCH(QString, text);
    QFETCH(int, major);
    QFETCH(int, minor);
    QFETCH(int, patch);
    QFETCH(QStringList, preRelease);
    QFETCH(QString, build);

    const std::optional<SemVer> parsed = SemVer::parse(text);
    QVERIFY2(parsed.has_value(), qPrintable(text));
    QCOMPARE(parsed->major, major);
    QCOMPARE(parsed->minor, minor);
    QCOMPARE(parsed->patch, patch);
    QCOMPARE(parsed->preRelease, preRelease);
    QCOMPARE(parsed->build, build);
    QCOMPARE(parsed->isPreRelease(), !preRelease.isEmpty());
}

void SemVerTests::invalidVersions_data()
{
    QTest::addColumn<QString>("text");

    QTest::newRow("empty") << QString();
    QTest::newRow("only v") << u"v"_s;
    QTest::newRow("one number") << u"1"_s;
    QTest::newRow("two numbers") << u"1.2"_s;
    QTest::newRow("four numbers") << u"1.2.3.4"_s;
    QTest::newRow("leading zero major") << u"01.2.3"_s;
    QTest::newRow("leading zero minor") << u"1.02.3"_s;
    QTest::newRow("leading zero patch") << u"1.2.03"_s;
    QTest::newRow("empty pre-release") << u"1.2.3-"_s;
    QTest::newRow("numeric pre-release with leading zero") << u"1.2.3-01"_s;
    QTest::newRow("empty pre-release identifier") << u"1.2.3-a..b"_s;
    QTest::newRow("empty build") << u"1.2.3+"_s;
    QTest::newRow("empty build identifier") << u"1.2.3+a..b"_s;
    QTest::newRow("letters") << u"a.b.c"_s;
    QTest::newRow("letter in patch") << u"1.2.x"_s;
    QTest::newRow("leading space") << u" 1.2.3"_s;
    QTest::newRow("trailing space") << u"1.2.3 "_s;
    QTest::newRow("trailing newline") << u"1.2.3\n"_s;
    QTest::newRow("two prefixes") << u"vv1.2.3"_s;
    QTest::newRow("other prefix") << u"release-1.2.3"_s;
    QTest::newRow("prefix with name") << u"KERS_SubsystemsV0.3.1"_s;
    QTest::newRow("comma") << u"1,2,3"_s;
    QTest::newRow("negative") << u"-1.2.3"_s;
    QTest::newRow("underscore in pre-release") << u"1.2.3-alpha_1"_s;
    QTest::newRow("symbol in pre-release") << u"1.2.3-@"_s;
    QTest::newRow("non-ASCII digit") << u"1.2.3١"_s;
    QTest::newRow("ten digit number") << u"1000000000.0.0"_s;
    QTest::newRow("much too long") << u"1.0.0-"_s + QString(130, u'a');
}

void SemVerTests::invalidVersions()
{
    QFETCH(QString, text);
    QVERIFY2(!SemVer::parse(text).has_value(), qPrintable(text));
}

void SemVerTests::largestAcceptedNumbers()
{
    const std::optional<SemVer> big = SemVer::parse(u"999999999.999999999.999999999"_s);
    QVERIFY(big.has_value());
    QCOMPARE(big->major, 999999999);
    QVERIFY(version(u"999999999.0.0"_s) > version(u"999999998.999999999.999999999"_s));
}

void SemVerTests::specificationOrder()
{
    // 1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < 1.0.0-beta < 1.0.0-beta.2 < 1.0.0-beta.11
    //   < 1.0.0-rc.1 < 1.0.0, continued with a few releases.
    const QStringList ascending = {u"1.0.0-alpha"_s, u"1.0.0-alpha.1"_s, u"1.0.0-alpha.beta"_s,
        u"1.0.0-beta"_s, u"1.0.0-beta.2"_s, u"1.0.0-beta.11"_s, u"1.0.0-rc.1"_s, u"1.0.0"_s, u"1.0.1"_s,
        u"1.1.0"_s, u"2.0.0-rc.1"_s, u"2.0.0"_s, u"10.0.0"_s};

    for (qsizetype i = 0; i < ascending.size(); ++i) {
        for (qsizetype j = 0; j < ascending.size(); ++j) {
            const SemVer a = version(ascending.at(i));
            const SemVer b = version(ascending.at(j));
            const QString what = ascending.at(i) + u" against "_s + ascending.at(j);
            if (i < j) {
                QVERIFY2(a < b, qPrintable(what));
                QVERIFY2(b > a, qPrintable(what));
                QVERIFY2(a != b, qPrintable(what));
            } else if (i == j) {
                QVERIFY2(a == b, qPrintable(what));
            } else {
                QVERIFY2(a > b, qPrintable(what));
            }
        }
    }
}

void SemVerTests::numbersAreComparedAsNumbers()
{
    // Compared as text, "9" would be larger than "10".
    QVERIFY(version(u"1.9.0"_s) < version(u"1.10.0"_s));
    QVERIFY(version(u"1.0.9"_s) < version(u"1.0.10"_s));
    QVERIFY(version(u"2.0.0"_s) < version(u"10.0.0"_s));
    QVERIFY(version(u"1.0.0-rc.9"_s) < version(u"1.0.0-rc.10"_s));
}

void SemVerTests::preReleaseIsBelowRelease()
{
    QVERIFY(version(u"1.0.0-rc.1"_s) < version(u"1.0.0"_s));
    QVERIFY(version(u"1.0.0"_s) > version(u"1.0.0-zzz"_s));
    // ...but above the release before it.
    QVERIFY(version(u"1.0.0-alpha"_s) > version(u"0.9.9"_s));
}

void SemVerTests::preReleaseIdentifiers()
{
    // Numbers rank below text, whatever the digits say.
    QVERIFY(version(u"1.0.0-1"_s) < version(u"1.0.0-a"_s));
    QVERIFY(version(u"1.0.0-99999"_s) < version(u"1.0.0-a"_s));
    // Text is compared by character code, so capitals come first.
    QVERIFY(version(u"1.0.0-A"_s) < version(u"1.0.0-a"_s));
    QVERIFY(version(u"1.0.0-a1"_s) < version(u"1.0.0-a2"_s));
    // With equal identifiers, the shorter list is smaller.
    QVERIFY(version(u"1.0.0-a"_s) < version(u"1.0.0-a.0"_s));
    // A number that does not fit in 64 bits still compares correctly.
    QVERIFY(version(u"1.0.0-99999999999999999999"_s) < version(u"1.0.0-100000000000000000000"_s));
}

void SemVerTests::buildMetadataIsIgnored()
{
    QVERIFY(version(u"1.0.0+a"_s) == version(u"1.0.0+b"_s));
    QVERIFY(version(u"1.0.0+a"_s) == version(u"1.0.0"_s));
    QCOMPARE(version(u"1.0.0+a"_s) <=> version(u"1.0.0+z"_s), std::strong_ordering::equal);
    QVERIFY(version(u"1.0.0-rc.1+build9"_s) < version(u"1.0.0+build1"_s));
    QVERIFY(version(u"1.0.1+a"_s) > version(u"1.0.0+z"_s));
}

void SemVerTests::equalVersionsAreEqual()
{
    QVERIFY(version(u"v1.2.3"_s) == version(u"1.2.3"_s));
    QVERIFY(!(version(u"1.2.3"_s) < version(u"v1.2.3"_s)));
    QVERIFY(version(u"1.2.3"_s) <= version(u"1.2.3"_s));
    QVERIFY(version(u"1.2.3"_s) >= version(u"1.2.3"_s));
}

void SemVerTests::newestOfAListDoesNotDependOnItsOrder()
{
    // A hotfix for an old line may be published after the newest release.
    QList<SemVer> versions;
    for (const QString& text : {u"1.4.0"_s, u"2.0.0-rc.1"_s, u"1.4.1"_s, u"2.0.0"_s, u"1.10.0"_s, u"1.9.9"_s})
        versions.append(version(text));

    QCOMPARE(std::ranges::max(versions).toString(), u"2.0.0"_s);
    std::ranges::sort(versions);
    QCOMPARE(versions.first().toString(), u"1.4.0"_s);
    QCOMPARE(versions.at(2).toString(), u"1.9.9"_s);
}

void SemVerTests::toStringRoundTrips()
{
    for (const QString& text :
        {u"1.2.3"_s, u"1.0.0-alpha.1"_s, u"1.0.0-beta+exp.sha.5114f85"_s, u"1.0.0+001"_s}) {
        QCOMPARE(version(text).toString(), text);
    }
    // The prefix of a tag is not part of the version.
    QCOMPARE(version(u"v2.0.0"_s).toString(), u"2.0.0"_s);
}

QTEST_GUILESS_MAIN(SemVerTests)
#include "SemVerTests.moc"
