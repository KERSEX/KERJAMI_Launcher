// Log file handling: format, rotation, the size limit and path redaction.

#include "core/Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using launcher::lcApp;
using launcher::lcSettings;
using launcher::Logger;

class LoggerTests : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void writesTimestampLevelAndCategory();
    void debugOutputIsOffByDefault();
    void appendsAcrossSessions();
    void secondInstallIsRejected();
    void rotatesAndKeepsTheConfiguredNumberOfFiles();
    void withoutBackupsTheFileStartsOver();
    void redactsThePortableRoot();
    void indentsContinuationLines();
    void truncatesOversizedMessages();

private:
    Logger::Options options() const
    {
        Logger::Options result;
        result.directory = m_dir->filePath(u"logs"_s);
        return result;
    }

    QString logPath(int index = 0) const
    {
        const QString suffix = index == 0 ? u".log"_s : u".%1.log"_s.arg(index);
        return m_dir->filePath(u"logs/launcher"_s + suffix);
    }

    static QString read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

    std::unique_ptr<QTemporaryDir> m_dir;
};

void LoggerTests::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
}

void LoggerTests::cleanup()
{
    Logger::uninstall(); // in case a test failed before its own uninstall
    m_dir.reset();
}

void LoggerTests::writesTimestampLevelAndCategory()
{
    QVERIFY(Logger::install(options()));
    QCOMPARE(Logger::currentFilePath(), logPath());

    qCInfo(lcApp) << "first entry";
    qCWarning(lcSettings) << "second entry";
    qCCritical(lcApp) << "third entry";
    Logger::uninstall();
    QVERIFY(Logger::currentFilePath().isEmpty());

    const QStringList lines = read(logPath()).split(u'\n', Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 3);

    // 2026-11-09T14:03:22.123 INFO  [app] first entry
    static const QRegularExpression pattern(
        u"^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}\\.\\d{3} (INFO |WARN |ERROR) \\[(app|settings)\\] .+$"_s);
    for (const QString& line : lines)
        QVERIFY2(pattern.match(line).hasMatch(), qPrintable(line));

    QVERIFY(lines.at(0).endsWith(u"INFO  [app] first entry"_s));
    QVERIFY(lines.at(1).endsWith(u"WARN  [settings] second entry"_s));
    QVERIFY(lines.at(2).endsWith(u"ERROR [app] third entry"_s));
}

void LoggerTests::debugOutputIsOffByDefault()
{
    QVERIFY(Logger::install(options()));
    qCDebug(lcApp) << "noise";
    qCInfo(lcApp) << "signal";
    Logger::uninstall();

    const QString content = read(logPath());
    QVERIFY(content.contains(u"signal"_s));
    QVERIFY(!content.contains(u"noise"_s));
}

void LoggerTests::appendsAcrossSessions()
{
    QVERIFY(Logger::install(options()));
    qCInfo(lcApp) << "session one";
    Logger::uninstall();

    QVERIFY(Logger::install(options()));
    qCInfo(lcApp) << "session two";
    Logger::uninstall();

    const QString content = read(logPath());
    QVERIFY(content.indexOf(u"session one"_s) >= 0);
    QVERIFY(content.indexOf(u"session two"_s) > content.indexOf(u"session one"_s));
}

void LoggerTests::secondInstallIsRejected()
{
    QVERIFY(Logger::install(options()));
    QVERIFY(!Logger::install(options()));
    Logger::uninstall();
}

void LoggerTests::rotatesAndKeepsTheConfiguredNumberOfFiles()
{
    Logger::Options config = options();
    config.maxFileSize = 2048;
    config.maxBackupFiles = 2;
    QVERIFY(Logger::install(config));

    // Roughly 26 KB of output against a budget of 3 x 2 KB.
    for (int i = 0; i < 400; ++i)
        qCInfo(lcApp) << "rotation line" << i;
    Logger::uninstall();

    QVERIFY(QFileInfo::exists(logPath(0)));
    QVERIFY(QFileInfo::exists(logPath(1)));
    QVERIFY(QFileInfo::exists(logPath(2)));
    QVERIFY(!QFileInfo::exists(logPath(3)));

    qint64 total = 0;
    for (int index = 0; index <= 2; ++index) {
        const qint64 size = QFileInfo(logPath(index)).size();
        QVERIFY2(size <= config.maxFileSize, qPrintable(logPath(index)));
        QVERIFY(size > 0);
        total += size;
    }
    QVERIFY(total <= 3 * config.maxFileSize);

    // Newest entries are in the current file, older ones in higher numbers,
    // and the oldest are gone.
    QVERIFY(read(logPath(0)).contains(u"rotation line 399"_s));
    QVERIFY(!read(logPath(0)).contains(u"rotation line 300\n"_s));
    const QString everything = read(logPath(0)) + read(logPath(1)) + read(logPath(2));
    QVERIFY(!everything.contains(u"rotation line 0\n"_s));

    // No entry is cut in half by a rotation.
    for (int index = 0; index <= 2; ++index)
        QVERIFY(read(logPath(index)).endsWith(u'\n'));
}

void LoggerTests::withoutBackupsTheFileStartsOver()
{
    Logger::Options config = options();
    config.maxFileSize = 2048;
    config.maxBackupFiles = 0;
    QVERIFY(Logger::install(config));
    for (int i = 0; i < 200; ++i)
        qCInfo(lcApp) << "single file line" << i;
    Logger::uninstall();

    QCOMPARE(QDir(m_dir->filePath(u"logs"_s)).entryList(QDir::Files), QStringList {u"launcher.log"_s});
    QVERIFY(QFileInfo(logPath()).size() <= config.maxFileSize);
    QVERIFY(read(logPath()).contains(u"single file line 199"_s));
}

void LoggerTests::redactsThePortableRoot()
{
    // Stands in for a folder below the user profile, e.g. C:/Users/<name>/Launcher.
    const QString root = m_dir->filePath(u"Private Name/Launcher"_s);
    QVERIFY(QDir().mkpath(root));

    Logger::Options config = options();
    config.redactRoot = root;
    QVERIFY(Logger::install(config));
    qCInfo(lcApp).noquote() << "forward" << root + u"/library/Demo"_s;
    qCInfo(lcApp).noquote() << "native" << QDir::toNativeSeparators(root + u"/library/Demo"_s);
    qCInfo(lcApp).noquote() << "upper" << root.toUpper() + u"/CACHE"_s;
    Logger::uninstall();

    const QString content = read(logPath());
    QVERIFY(content.contains(u"forward <root>/library/Demo"_s));
    QVERIFY(content.contains(u"native <root>\\library\\Demo"_s));
    QVERIFY(content.contains(u"upper <root>/CACHE"_s));
    QVERIFY(!content.contains(u"Private Name"_s, Qt::CaseInsensitive));
}

void LoggerTests::indentsContinuationLines()
{
    QVERIFY(Logger::install(options()));
    qCInfo(lcApp).noquote() << u"headline\ndetail one\ndetail two"_s;
    Logger::uninstall();

    QVERIFY(read(logPath()).endsWith(u"[app] headline\n    detail one\n    detail two\n"_s));
}

void LoggerTests::truncatesOversizedMessages()
{
    QVERIFY(Logger::install(options()));
    qCInfo(lcApp).noquote() << QString(100'000, u'x');
    Logger::uninstall();

    const QString content = read(logPath());
    QVERIFY(content.size() < 10'000);
    QVERIFY(content.endsWith(u" [truncated]\n"_s));
}

QTEST_GUILESS_MAIN(LoggerTests)
#include "LoggerTests.moc"
