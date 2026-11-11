// The storage of the portable folder: how much room downloads, temporary files and
// the backups of earlier versions take, and cleaning them up. Cleaning up deletes
// files, so the tests look hard at what it must NOT touch: installed versions, files
// outside the folder, and anything reached through a link.

#include "core/PathManager.h"
#include "installer/Storage.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <memory>

using namespace Qt::StringLiterals;
using launcher::PathManager;
namespace storage = launcher::Storage;

namespace {

bool writeBytes(const QString& path, int size)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QByteArray(size, 'x')) == size;
}

/// A directory junction needs no special rights on Windows.
bool makeJunction(const QString& link, const QString& target)
{
    QProcess process;
    process.start(u"cmd.exe"_s,
        {u"/c"_s, u"mklink"_s, u"/J"_s, QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)});
    return process.waitForFinished(10000) && process.exitCode() == 0;
}

} // namespace

class StorageTests : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_root;
    std::unique_ptr<PathManager> m_paths;

    QString library(const QString& relative) const { return m_paths->libraryDir() + u'/' + relative; }

private slots:
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QVERIFY(m_root->isValid());
        m_paths = std::make_unique<PathManager>(m_root->filePath(u"app"_s));
        QVERIFY(QDir().mkpath(m_paths->root()));
        QVERIFY(m_paths->prepare().ok());
    }

    void cleanup()
    {
        m_paths.reset();
        m_root.reset();
    }

    void anEmptyFolderTakesNothing()
    {
        const storage::Usage usage = storage::measure(*m_paths);
        QCOMPARE(usage.downloadsAndTemp, qint64(0));
        QCOMPARE(usage.backups, qint64(0));
    }

    void measuresDownloadsTempAndBackups()
    {
        QVERIFY(writeBytes(m_paths->downloadsDir() + u"/tool/Tool.zip"_s, 1000));
        QVERIFY(writeBytes(m_paths->tempDir() + u"/download-1.part"_s, 200));
        QVERIFY(writeBytes(m_paths->tempDir() + u"/staging-tool/bin/Tool.exe"_s, 30));
        QVERIFY(writeBytes(library(u"Tool/current/Tool.exe"_s), 5000)); // installed: not counted
        QVERIFY(writeBytes(library(u"Tool/backups/1.0.0/Tool.exe"_s), 400));
        QVERIFY(writeBytes(library(u"Tool/backups/1.1.0/data/a.bin"_s), 50));
        QVERIFY(writeBytes(library(u"Other/backups/2.0.0/Other.exe"_s), 7));

        const storage::Usage usage = storage::measure(*m_paths);
        QCOMPARE(usage.downloadsAndTemp, qint64(1230));
        QCOMPARE(usage.backups, qint64(457));
    }

    void measuringCanBeStopped()
    {
        for (int i = 0; i < 20; ++i)
            QVERIFY(writeBytes(m_paths->downloadsDir() + u"/f%1.bin"_s.arg(i), 10));
        std::atomic<bool> stop {true};
        const storage::Usage usage = storage::measure(*m_paths, &stop);
        QVERIFY(usage.downloadsAndTemp < 200); // it stopped on the way
    }

    void clearingDownloadsAndTempKeepsTheFoldersAndEverythingElse()
    {
        QVERIFY(writeBytes(m_paths->downloadsDir() + u"/tool/Tool.zip"_s, 1000));
        QVERIFY(writeBytes(m_paths->tempDir() + u"/download-1.part"_s, 200));
        QVERIFY(writeBytes(m_paths->tempDir() + u"/staging-tool/bin/Tool.exe"_s, 30));
        QVERIFY(writeBytes(library(u"Tool/current/Tool.exe"_s), 5000));
        QVERIFY(writeBytes(library(u"Tool/backups/1.0.0/Tool.exe"_s), 400));
        QVERIFY(writeBytes(m_paths->settingsFile(), 20));

        QString error;
        QVERIFY2(storage::clearDownloadsAndTemp(*m_paths, &error), qPrintable(error));

        QVERIFY(QDir(m_paths->downloadsDir()).isEmpty());
        QVERIFY(QDir(m_paths->tempDir()).isEmpty());
        QVERIFY(QFileInfo(m_paths->downloadsDir()).isDir()); // the folders themselves stay
        QVERIFY(QFileInfo(m_paths->tempDir()).isDir());
        QVERIFY(QFileInfo::exists(library(u"Tool/current/Tool.exe"_s)));
        QVERIFY(QFileInfo::exists(library(u"Tool/backups/1.0.0/Tool.exe"_s))); // not this cleanup's business
        QVERIFY(QFileInfo::exists(m_paths->settingsFile()));
        QCOMPARE(storage::measure(*m_paths).downloadsAndTemp, qint64(0));
    }

    void clearingBackupsKeepsTheInstalledVersions()
    {
        QVERIFY(writeBytes(library(u"Tool/current/Tool.exe"_s), 5000));
        QVERIFY(writeBytes(library(u"Tool/backups/1.0.0/Tool.exe"_s), 400));
        QVERIFY(writeBytes(library(u"Tool/backups/1.1.0/Tool.exe"_s), 410));
        QVERIFY(writeBytes(library(u"Other/current/Other.exe"_s), 9));
        QVERIFY(writeBytes(library(u"Other/backups/2.0.0/Other.exe"_s), 7));
        QVERIFY(writeBytes(m_paths->downloadsDir() + u"/tool/Tool.zip"_s, 1000));

        QString error;
        QVERIFY2(storage::clearBackups(*m_paths, &error), qPrintable(error));

        QVERIFY(QDir(library(u"Tool/backups"_s)).isEmpty());
        QVERIFY(QDir(library(u"Other/backups"_s)).isEmpty());
        QVERIFY(QFileInfo(library(u"Tool/backups"_s)).isDir());
        QVERIFY(QFileInfo::exists(library(u"Tool/current/Tool.exe"_s)));
        QVERIFY(QFileInfo::exists(library(u"Other/current/Other.exe"_s)));
        QVERIFY(QFileInfo::exists(m_paths->downloadsDir() + u"/tool/Tool.zip"_s));
        QCOMPARE(storage::measure(*m_paths).backups, qint64(0));
    }

    void aLinkIsNeverFollowed()
    {
        // A folder outside the launcher's folder, reached through a junction in downloads/,
        // in temp/ and as a project's backups folder.
        QTemporaryDir outside;
        QVERIFY(outside.isValid());
        QVERIFY(writeBytes(outside.filePath(u"precious.txt"_s), 500));

        if (!makeJunction(m_paths->downloadsDir() + u"/linked"_s, outside.path()))
            QSKIP("directory junctions cannot be created here");
        QVERIFY(makeJunction(m_paths->tempDir() + u"/linked"_s, outside.path()));
        QVERIFY(writeBytes(library(u"Tool/current/Tool.exe"_s), 5));
        QVERIFY(makeJunction(library(u"Tool/backups"_s), outside.path()));
        QVERIFY(makeJunction(library(u"Linked"_s), outside.path())); // a whole project folder that is a link

        // The size of what a link points to is not counted.
        const storage::Usage usage = storage::measure(*m_paths);
        QVERIFY2(usage.downloadsAndTemp < 500, qPrintable(QString::number(usage.downloadsAndTemp)));
        QCOMPARE(usage.backups, qint64(0));

        // And nothing behind a link is deleted: the link goes, the target stays.
        QVERIFY(storage::clearDownloadsAndTemp(*m_paths));
        QVERIFY(storage::clearBackups(*m_paths));
        QVERIFY(QFileInfo::exists(outside.filePath(u"precious.txt"_s)));
        QCOMPARE(QFile(outside.filePath(u"precious.txt"_s)).size(), qint64(500));
        QVERIFY(QFileInfo::exists(library(u"Tool/current/Tool.exe"_s)));
    }
};

QTEST_GUILESS_MAIN(StorageTests)
#include "StorageTests.moc"
