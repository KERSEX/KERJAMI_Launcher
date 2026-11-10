// Security: SHA-256 verification, the strict ZIP reader, the archive validator
// and the extractor. Every safety rule has a test that breaks exactly that rule.

#include "ZipBuilder.h"
#include "installer/ArchiveExtractor.h"
#include "security/ArchiveValidator.h"
#include "security/HashVerifier.h"
#include "security/ZipArchive.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

using namespace Qt::StringLiterals;
using launcher::ZipArchive;
namespace validator = launcher::ArchiveValidator;
namespace hash = launcher::HashVerifier;
namespace extractor = launcher::ArchiveExtractor;

namespace {

QByteArray payload(qint64 size)
{
    QByteArray data(size, '\0');
    for (qint64 i = 0; i < size; ++i)
        data[i] = static_cast<char>((i * 31 + 7) & 0xFF);
    return data;
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

const QString kAbc = u"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"_s;
const QString kEmpty = u"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"_s;

} // namespace

class SecurityTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString path(const QString& name) const { return m_dir.filePath(name); }

    /// Writes an archive and opens it.
    ZipArchive::OpenResult open(const QByteArray& archive)
    {
        const QString file = path(u"test.zip"_s);
        [&] { QVERIFY(writeFile(file, archive)); }();
        return ZipArchive::open(file);
    }

    /// Reads one entry completely; returns the extraction status.
    ZipArchive::ExtractStatus extractEntry(ZipArchive& archive, int index, QByteArray* data = nullptr)
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        const ZipArchive::ExtractResult result = archive.extract(archive.entries().at(index), buffer);
        if (data != nullptr)
            *data = buffer.data();
        return result.status;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    // --- SHA-256 ------------------------------------------------------------

    void hash_knownValues()
    {
        QVERIFY(writeFile(path(u"abc.bin"_s), "abc"));
        QCOMPARE(hash::sha256File(path(u"abc.bin"_s)), kAbc);
        QVERIFY(writeFile(path(u"empty.bin"_s), ""));
        QCOMPARE(hash::sha256File(path(u"empty.bin"_s)), kEmpty);
    }

    void hash_largeFileInChunks()
    {
        // Larger than one read chunk and not a multiple of it.
        const QByteArray data = payload(2'500'000);
        QVERIFY(writeFile(path(u"large.bin"_s), data));
        const QString expected =
            QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());

        qint64 last = 0;
        int calls = 0;
        const QString actual =
            hash::sha256File(path(u"large.bin"_s), nullptr, nullptr, [&](qint64 processed) {
                QVERIFY(processed > last);
                last = processed;
                ++calls;
            });
        QCOMPARE(actual, expected);
        QCOMPARE(last, data.size());
        QVERIFY(calls >= 2);
    }

    void hash_verify_data()
    {
        QVERIFY(writeFile(path(u"abc.bin"_s), "abc"));
        const QString file = path(u"abc.bin"_s);

        QCOMPARE(hash::verifySha256(file, kAbc).status, hash::Status::Match);
        QCOMPARE(hash::verifySha256(file, kAbc.toUpper()).status, hash::Status::Match);
        QCOMPARE(hash::verifySha256(file, u"  "_s + kAbc + u"\n"_s).status, hash::Status::Match);

        const hash::VerifyResult wrong = hash::verifySha256(file, kEmpty);
        QCOMPARE(wrong.status, hash::Status::Mismatch);
        QCOMPARE(wrong.actual, kAbc);
        QVERIFY(!wrong.ok());

        // A file without a hash to compare with is not "verified".
        QCOMPARE(hash::verifySha256(file, QString()).status, hash::Status::NoExpectedHash);
        QCOMPARE(hash::verifySha256(file, u"   "_s).status, hash::Status::NoExpectedHash);
        QCOMPARE(hash::verifySha256(file, u"abc"_s).status, hash::Status::InvalidExpectedHash);
        QCOMPARE(hash::verifySha256(file, kAbc.left(63)).status, hash::Status::InvalidExpectedHash);
        QCOMPARE(hash::verifySha256(file, kAbc + u"0"_s).status, hash::Status::InvalidExpectedHash);
        QCOMPARE(hash::verifySha256(file, u"sha256:"_s + kAbc).status, hash::Status::InvalidExpectedHash);
        QCOMPARE(hash::verifySha256(file, kAbc.left(63) + u"g"_s).status, hash::Status::InvalidExpectedHash);

        QCOMPARE(hash::verifySha256(path(u"missing.bin"_s), kAbc).status, hash::Status::FileError);
    }

    void hash_cancel()
    {
        QVERIFY(writeFile(path(u"large.bin"_s), payload(3'000'000)));
        std::atomic<bool> cancel = true;
        QCOMPARE(hash::verifySha256(path(u"large.bin"_s), kAbc, &cancel).status, hash::Status::Cancelled);
    }

    void hash_isValidSha256()
    {
        QVERIFY(hash::isValidSha256(kAbc));
        QVERIFY(hash::isValidSha256(kAbc.toUpper()));
        QVERIFY(!hash::isValidSha256(QString()));
        QVERIFY(!hash::isValidSha256(kAbc.left(63)));
        QVERIFY(!hash::isValidSha256(kAbc + u"a"_s));
        QVERIFY(!hash::isValidSha256(u"zz"_s.repeated(32)));
    }

    // --- checksum files ---------------------------------------------------

    void checksum_formats()
    {
        const QString name = u"Tool-Windows-x64.zip"_s;
        const auto find = [&](const QString& text, bool bare = false) {
            return hash::findInChecksumFile(text.toUtf8(), name, bare);
        };

        QCOMPARE(find(kAbc + u"  "_s + name + u"\n"_s), kAbc);
        QCOMPARE(find(kAbc + u" *"_s + name + u"\r\n"_s), kAbc);
        QCOMPARE(find(kAbc.toUpper() + u"  ./"_s + name), kAbc);
        QCOMPARE(
            find(u"# produced by CI\n\n"_s + kEmpty + u"  other.zip\n"_s + kAbc + u"  "_s + name + u"\n"_s),
            kAbc);

        // A line with only a hash counts for a file that belongs to a single asset only.
        QCOMPARE(find(kAbc + u"\n"_s, true), kAbc);
        QVERIFY(find(kAbc + u"\n"_s, false).isEmpty());
    }

    void checksum_refusals()
    {
        const QString name = u"Tool.zip"_s;
        const auto find = [&](const QString& text) {
            return hash::findInChecksumFile(text.toUtf8(), name, false);
        };

        QVERIFY(find(QString()).isEmpty());
        QVERIFY(find(kAbc + u"  other.zip\n"_s).isEmpty()); // names another file
        QVERIFY(find(kAbc + u"  tool.zip\n"_s).isEmpty());  // exact match only
        QVERIFY(find(kAbc + u"  "_s + name + u"\n"_s + kEmpty + u"  "_s + name).isEmpty()); // two answers
        QVERIFY(find(u"not a checksum line\n"_s + kAbc + u"  "_s + name).isEmpty()); // malformed content
        QVERIFY(find(kAbc.left(63) + u"  "_s + name).isEmpty());
        QVERIFY(hash::findInChecksumFile(QByteArray(70 * 1024, 'a'), name, false).isEmpty()); // too large
        QVERIFY(hash::findInChecksumFile((kAbc + u"  "_s + name).toUtf8(), QString(), true).isEmpty());

        // The same hash twice is not a contradiction.
        QCOMPARE(find(kAbc + u"  "_s + name + u"\n"_s + kAbc + u" *"_s + name + u"\n"_s), kAbc);
    }

    // --- ZIP reader: structure ----------------------------------------------

    void zip_roundTrip()
    {
        const QByteArray text = "hello world hello world hello world\n";
        const QByteArray binary = payload(300'000);
        const auto opened = open(testzip::build({
            testzip::folder(u"bin/"_s),
            testzip::file(u"bin/App.exe"_s, binary, true),
            testzip::file(u"readme.txt"_s, text, false),
            testzip::file(u"empty.txt"_s, "", true),
            testzip::file(u"Überprüfung/ä.txt"_s, text),
        }));
        QVERIFY2(opened.ok(), qPrintable(opened.detail));
        ZipArchive& zip = *opened.archive;
        QCOMPARE(zip.entries().size(), 5);

        QCOMPARE(zip.entries().at(0).kind, ZipArchive::EntryKind::Directory);
        QCOMPARE(zip.entries().at(1).name, u"bin/App.exe"_s);
        QCOMPARE(zip.entries().at(1).method, quint16(8));
        QCOMPARE(zip.entries().at(2).method, quint16(0));
        QCOMPARE(zip.entries().at(4).name, u"Überprüfung/ä.txt"_s);

        QByteArray data;
        QCOMPARE(extractEntry(zip, 1, &data), ZipArchive::ExtractStatus::Ok);
        QCOMPARE(data, binary);
        QCOMPARE(extractEntry(zip, 2, &data), ZipArchive::ExtractStatus::Ok);
        QCOMPARE(data, text);
        QCOMPARE(extractEntry(zip, 3, &data), ZipArchive::ExtractStatus::Ok);
        QVERIFY(data.isEmpty());
        QCOMPARE(extractEntry(zip, 4, &data), ZipArchive::ExtractStatus::Ok);
        QCOMPARE(data, text);

        // A folder is not extracted as a file.
        QVERIFY(extractEntry(zip, 0) != ZipArchive::ExtractStatus::Ok);
    }

    void zip_archiveComment()
    {
        const auto opened = open(testzip::build({testzip::file(u"a.txt"_s, "x")}, "built by a tool"));
        QVERIFY(opened.ok());
        QCOMPARE(opened.archive->entries().size(), 1);
    }

    void zip_notAnArchive()
    {
        QCOMPARE(open(QByteArray()).error, ZipArchive::Error::NotAZip);
        QCOMPARE(open(QByteArray("tiny")).error, ZipArchive::Error::NotAZip);
        QCOMPARE(open(payload(200'000)).error, ZipArchive::Error::NotAZip);
        QCOMPARE(ZipArchive::open(path(u"does-not-exist.zip"_s)).error, ZipArchive::Error::CannotOpen);
    }

    void zip_truncatedAndExtended()
    {
        const QByteArray good = testzip::build({testzip::file(u"a.txt"_s, payload(5000))});
        QVERIFY(open(good).ok());
        QCOMPARE(open(good.left(good.size() - 1)).error, ZipArchive::Error::NotAZip);
        QCOMPARE(open(good.left(good.size() / 2)).error, ZipArchive::Error::NotAZip);
        // Bytes after the end record: another tool would see a different archive.
        QCOMPARE(open(good + "trailing garbage").error, ZipArchive::Error::NotAZip);
        // Bytes in front (a self-extracting stub) move every offset.
        QCOMPARE(open(QByteArray("MZ stub") + good).error, ZipArchive::Error::Corrupt);
    }

    void zip_unsupportedFeatures()
    {
        // encrypted
        testzip::Entry encrypted = testzip::file(u"a.txt"_s, "x");
        encrypted.flags = 0x0801;
        QCOMPARE(open(testzip::build({encrypted})).error, ZipArchive::Error::Unsupported);

        // other compression methods
        testzip::Entry bzip = testzip::file(u"a.txt"_s, "x");
        bzip.method = 12;
        QCOMPARE(open(testzip::build({bzip})).error, ZipArchive::Error::Unsupported);

        // ZIP64 markers
        testzip::Entry huge = testzip::file(u"a.txt"_s, "x");
        huge.size = 0xFFFFFFFFu;
        QCOMPARE(open(testzip::build({huge})).error, ZipArchive::Error::Unsupported);

        // names in an unknown encoding
        testzip::Entry latin1;
        latin1.name = QByteArray("caf\xE9.txt");
        latin1.data = "x";
        QCOMPARE(open(testzip::build({latin1})).error, ZipArchive::Error::Unsupported);
    }

    void zip_contradictions()
    {
        // The local header carries another name than the central directory.
        testzip::Entry renamed = testzip::file(u"safe.txt"_s, "x");
        renamed.localName = "evil!!.txt";
        QCOMPARE(open(testzip::build({renamed})).error, ZipArchive::Error::Corrupt);

        // Same length, other content.
        testzip::Entry sameLength = testzip::file(u"safe.txt"_s, "x");
        sameLength.localName = "other.txt";
        QCOMPARE(open(testzip::build({sameLength})).error, ZipArchive::Error::Corrupt);

        // The central directory points to a place without a local header.
        testzip::Entry lost = testzip::file(u"a.txt"_s, "x");
        lost.localOffset = 3;
        QCOMPARE(open(testzip::build({lost})).error, ZipArchive::Error::Corrupt);
        testzip::Entry outside = testzip::file(u"a.txt"_s, "x");
        outside.localOffset = 5'000'000;
        QCOMPARE(open(testzip::build({outside})).error, ZipArchive::Error::Corrupt);

        // A name and the type recorded by the archiver disagree.
        testzip::Entry fileWithSlash = testzip::file(u"dir/"_s, "x");
        QCOMPARE(open(testzip::build({fileWithSlash})).error, ZipArchive::Error::Corrupt);
        testzip::Entry folderWithoutSlash = testzip::folder(u"dir"_s);
        QCOMPARE(open(testzip::build({folderWithoutSlash})).error, ZipArchive::Error::Corrupt);

        // A stored entry with two different sizes.
        testzip::Entry stored = testzip::file(u"a.txt"_s, "xyz", false);
        stored.rawCompressed = QByteArray("xy");
        QCOMPARE(open(testzip::build({stored})).error, ZipArchive::Error::Corrupt);

        // An empty or invalid name.
        testzip::Entry empty;
        empty.name = "";
        QCOMPARE(open(testzip::build({empty})).error, ZipArchive::Error::Corrupt);
        testzip::Entry nul;
        nul.name = QByteArray("a\0b.txt", 7);
        QCOMPARE(open(testzip::build({nul})).error, ZipArchive::Error::Corrupt);
        testzip::Entry badUtf8 = testzip::file(u"x"_s, "x");
        badUtf8.name = QByteArray("\xC3\x28.txt");
        badUtf8.localName = badUtf8.name;
        QCOMPARE(open(testzip::build({badUtf8})).error, ZipArchive::Error::Corrupt);
    }

    void zip_overlappingEntries()
    {
        // Two directory entries that share one stretch of data: how a small file
        // is made to "contain" a huge one.
        QByteArray archive = testzip::build({
            testzip::file(u"a.txt"_s, payload(2000), false),
            testzip::file(u"b.txt"_s, payload(2000), false),
        });
        QVERIFY(open(archive).ok());

        // Both directory entries describe the same local header and data.
        testzip::Entry first = testzip::file(u"a.txt"_s, payload(2000), false);
        testzip::Entry second = testzip::file(u"a.txt"_s, payload(2000), false);
        second.localOffset = 0;
        const auto opened = open(testzip::build({first, second}));
        QCOMPARE(opened.error, ZipArchive::Error::Corrupt);
        QVERIFY(opened.detail.contains(u"overlap"_s));
    }

    void zip_entryKinds()
    {
        testzip::Entry link = testzip::file(u"link"_s, "target.txt", false);
        link.unixMode = 0120777;
        testzip::Entry fifo = testzip::file(u"pipe"_s, "", false);
        fifo.unixMode = 010644;
        testzip::Entry dosFolder = testzip::folder(u"dos/"_s);
        dosFolder.hostSystem = 0;

        const auto opened = open(testzip::build({link, fifo, dosFolder, testzip::file(u"plain"_s, "x")}));
        QVERIFY(opened.ok());
        QCOMPARE(opened.archive->entries().at(0).kind, ZipArchive::EntryKind::Symlink);
        QCOMPARE(opened.archive->entries().at(1).kind, ZipArchive::EntryKind::Other);
        QCOMPARE(opened.archive->entries().at(2).kind, ZipArchive::EntryKind::Directory);
        QCOMPARE(opened.archive->entries().at(3).kind, ZipArchive::EntryKind::File);
    }

    // --- ZIP reader: data -----------------------------------------------------

    void zip_extractErrors()
    {
        const QByteArray data = payload(20'000);

        testzip::Entry badCrc = testzip::file(u"a.bin"_s, data);
        badCrc.crc = 0x12345678;
        auto opened = open(testzip::build({badCrc}));
        QVERIFY(opened.ok());
        QCOMPARE(extractEntry(*opened.archive, 0), ZipArchive::ExtractStatus::CrcMismatch);

        // Declared smaller than the real data: the reader stops instead of writing it all.
        testzip::Entry understated = testzip::file(u"a.bin"_s, data);
        understated.size = 100;
        opened = open(testzip::build({understated}));
        QVERIFY(opened.ok());
        QByteArray out;
        QCOMPARE(extractEntry(*opened.archive, 0, &out), ZipArchive::ExtractStatus::SizeMismatch);
        QVERIFY(out.size() <= 100);

        // Declared larger than the real data.
        testzip::Entry overstated = testzip::file(u"a.bin"_s, data);
        overstated.size = quint32(data.size() + 10);
        opened = open(testzip::build({overstated}));
        QVERIFY(opened.ok());
        QCOMPARE(extractEntry(*opened.archive, 0), ZipArchive::ExtractStatus::SizeMismatch);

        // Damaged compressed data.
        testzip::Entry garbage = testzip::file(u"a.bin"_s, data);
        garbage.rawCompressed = payload(500);
        opened = open(testzip::build({garbage}));
        QVERIFY(opened.ok());
        QCOMPARE(extractEntry(*opened.archive, 0), ZipArchive::ExtractStatus::CorruptData);

        // Compressed data that ends before the stream does.
        testzip::Entry cut = testzip::file(u"a.bin"_s, data);
        cut.rawCompressed = testzip::deflateRaw(data).left(30);
        opened = open(testzip::build({cut}));
        QVERIFY(opened.ok());
        QVERIFY(extractEntry(*opened.archive, 0) != ZipArchive::ExtractStatus::Ok);

        // Bytes after the end of the compressed stream.
        testzip::Entry extra = testzip::file(u"a.bin"_s, data);
        extra.rawCompressed = testzip::deflateRaw(data) + QByteArray(40, 'x');
        opened = open(testzip::build({extra}));
        QVERIFY(opened.ok());
        QCOMPARE(extractEntry(*opened.archive, 0), ZipArchive::ExtractStatus::SizeMismatch);
    }

    void zip_bombStopsAtDeclaredSize()
    {
        // 50 MB of zeros compress to about 50 KB. The entry claims 1 KB, so
        // nothing near 50 MB may be written.
        testzip::Entry bomb = testzip::file(u"zeros.bin"_s, QByteArray(50'000'000, '\0'));
        bomb.size = 1024;
        const auto opened = open(testzip::build({bomb}));
        QVERIFY(opened.ok());
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        const auto result = opened.archive->extract(opened.archive->entries().at(0), buffer);
        QCOMPARE(result.status, ZipArchive::ExtractStatus::SizeMismatch);
        QVERIFY2(buffer.size() <= 1024 + 64 * 1024, "wrote far more than declared");
        QVERIFY(buffer.size() <= 1024);
    }

    void zip_cancel()
    {
        const auto opened = open(testzip::build({testzip::file(u"a.bin"_s, payload(2'000'000))}));
        QVERIFY(opened.ok());
        std::atomic<bool> cancel = true;
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        QCOMPARE(opened.archive->extract(opened.archive->entries().at(0), buffer, &cancel).status,
            ZipArchive::ExtractStatus::Cancelled);
    }

    // --- path rules -----------------------------------------------------------

    void path_safeNames_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<bool>("directory");

        QTest::newRow("plain file") << u"App.exe"_s << u"App.exe"_s << false;
        QTest::newRow("nested") << u"bin/x64/App.exe"_s << u"bin/x64/App.exe"_s << false;
        QTest::newRow("folder") << u"data/"_s << u"data"_s << true;
        QTest::newRow("backslashes") << u"bin\\App.exe"_s << u"bin/App.exe"_s << false;
        QTest::newRow("folder with backslash") << u"data\\sub\\"_s << u"data/sub"_s << true;
        QTest::newRow("umlauts") << u"Überprüfung/Größe.txt"_s << u"Überprüfung/Größe.txt"_s << false;
        QTest::newRow("dots inside") << u"lib/foo.bar.dll"_s << u"lib/foo.bar.dll"_s << false;
        QTest::newRow("hidden file") << u".config"_s << u".config"_s << false;
        QTest::newRow("spaces inside") << u"My Files/a b.txt"_s << u"My Files/a b.txt"_s << false;
        QTest::newRow("device name as part") << u"console.txt"_s << u"console.txt"_s << false;
    }

    void path_safeNames()
    {
        QFETCH(QString, name);
        QFETCH(QString, expected);
        QFETCH(bool, directory);

        bool isDirectory = !directory;
        QString reason;
        QCOMPARE(validator::normalizeEntryPath(name, {}, &isDirectory, &reason), expected);
        QCOMPARE(isDirectory, directory);
    }

    void path_unsafeNames_data()
    {
        QTest::addColumn<QString>("name");

        QTest::newRow("empty") << QString();
        QTest::newRow("only slash") << u"/"_s;
        QTest::newRow("parent") << u"../evil.txt"_s;
        QTest::newRow("parent deep") << u"a/b/../../../evil.txt"_s;
        QTest::newRow("parent in the middle") << u"a/../b.txt"_s;
        QTest::newRow("parent with backslash") << u"a\\..\\..\\evil.txt"_s;
        QTest::newRow("parent only") << u".."_s;
        QTest::newRow("current dir") << u"./a.txt"_s;
        QTest::newRow("current dir inside") << u"a/./b.txt"_s;
        QTest::newRow("absolute") << u"/etc/passwd"_s;
        QTest::newRow("absolute backslash") << u"\\Windows\\evil.dll"_s;
        QTest::newRow("drive letter") << u"C:/Windows/evil.dll"_s;
        QTest::newRow("drive letter backslash") << u"C:\\evil.dll"_s;
        QTest::newRow("drive relative") << u"C:evil.dll"_s;
        QTest::newRow("unc") << u"\\\\server\\share\\evil.dll"_s;
        QTest::newRow("unc slash") << u"//server/share/evil.dll"_s;
        QTest::newRow("device path") << u"//?/C:/evil.dll"_s;
        QTest::newRow("alternate stream") << u"a.txt:hidden"_s;
        QTest::newRow("empty segment") << u"a//b.txt"_s;
        QTest::newRow("trailing dot") << u"a./b.txt"_s;
        QTest::newRow("trailing dot file") << u"b.txt."_s;
        QTest::newRow("trailing space") << u"a /b.txt"_s;
        QTest::newRow("reserved") << u"CON"_s;
        QTest::newRow("reserved lowercase with extension") << u"dir/nul.txt"_s;
        QTest::newRow("reserved com") << u"com1"_s;
        QTest::newRow("reserved lpt") << u"LPT9.log"_s;
        QTest::newRow("wildcard") << u"a*.txt"_s;
        QTest::newRow("question mark") << u"a?.txt"_s;
        QTest::newRow("pipe") << u"a|b.txt"_s;
        QTest::newRow("angle") << u"a<b>.txt"_s;
        QTest::newRow("quote") << u"a\"b.txt"_s;
        QTest::newRow("control") << QString(u"a\x01"
                                            "b.txt"_s);
        QTest::newRow("newline") << u"a\nb.txt"_s;
        QTest::newRow("delete") << QString(u"a\x7F"
                                           "b"_s);
        QTest::newRow("right-to-left override") << QString(u"exe\u202Etxt.fdp"_s);
        QTest::newRow("zero width space") << QString(u"a\u200Bb.txt"_s);
        QTest::newRow("lone surrogate") << QString(QChar(0xD800)) + u"a.txt"_s;
        QTest::newRow("long segment") << u"a"_s.repeated(129);
        QTest::newRow("long path") << (u"abcdefghij/"_s.repeated(25) + u"f.txt"_s);
        QTest::newRow("deep") << (u"d/"_s.repeated(30) + u"f.txt"_s);
    }

    void path_unsafeNames()
    {
        QFETCH(QString, name);
        QString reason;
        const QString result = validator::normalizeEntryPath(name, {}, nullptr, &reason);
        QVERIFY2(result.isEmpty(), qPrintable(u"accepted: "_s + result));
        QVERIFY(!reason.isEmpty());
    }

    // --- validator ------------------------------------------------------------

    validator::ValidationResult validate(const QList<testzip::Entry>& entries,
        const validator::Limits& limits = {}, const QString& executable = QString())
    {
        const auto opened = open(testzip::build(entries));
        validator::ValidationResult result;
        [&] { QVERIFY2(opened.ok(), qPrintable(opened.detail)); }();
        if (opened.ok())
            result = validator::validate(*opened.archive, limits, executable);
        return result;
    }

    void validator_acceptsGoodArchive()
    {
        const auto result = validate(
            {
                testzip::folder(u"bin/"_s),
                testzip::file(u"bin/App.exe"_s, payload(1000)),
                testzip::file(u"data\\config.json"_s, "{}"),
                testzip::file(u"readme.txt"_s, "hi", false),
            },
            {}, u"bin/App.exe"_s);
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QCOMPARE(result.items.size(), 4);
        QCOMPARE(result.items.at(0).path, u"bin"_s);
        QVERIFY(result.items.at(0).isDirectory);
        QCOMPARE(result.items.at(2).path, u"data/config.json"_s);
        QCOMPARE(result.totalSize, qint64(1000 + 2 + 2));
        QCOMPARE(result.items.at(1).entryIndex, 1);
    }

    void validator_executableIsMatchedIgnoringCase()
    {
        const auto result = validate({testzip::file(u"BIN/APP.EXE"_s, "x")}, {}, u"bin/App.exe"_s);
        QVERIFY(result.ok());
    }

    void validator_rejects()
    {
        using P = validator::Problem;
        const auto file = [](const QString& name, const QByteArray& data = "x") {
            return testzip::file(name, data);
        };

        QCOMPARE(validate({file(u"../evil.txt"_s)}).problem, P::UnsafePath);
        QCOMPARE(validate({file(u"ok.txt"_s), file(u"a/../../evil.txt"_s)}).problem, P::UnsafePath);
        QCOMPARE(validate({file(u"C:/evil.txt"_s)}).problem, P::UnsafePath);
        QCOMPARE(validate({file(u"/evil.txt"_s)}).problem, P::UnsafePath);
        QCOMPARE(validate({file(u"nul.txt"_s)}).problem, P::UnsafePath);

        QCOMPARE(validate({testzip::folder(u"empty/"_s)}).problem, P::Empty);
        QCOMPARE(validate({}).problem, P::Empty);

        // Names that are the same on Windows.
        QCOMPARE(validate({file(u"App.exe"_s), file(u"app.EXE"_s)}).problem, P::DuplicatePath);
        QCOMPARE(validate({file(u"a/b.txt"_s), file(u"a\\b.txt"_s)}).problem, P::DuplicatePath);
        QCOMPARE(validate({testzip::folder(u"d/"_s), testzip::folder(u"D/"_s), file(u"x"_s)}).problem,
            P::DuplicatePath);

        // One name as a file and as a folder.
        QCOMPARE(validate({file(u"tool"_s), file(u"tool/App.exe"_s)}).problem, P::ConflictingPath);
        QCOMPARE(validate({file(u"a/b/c.txt"_s), file(u"A/B"_s)}).problem, P::ConflictingPath);

        // Links and special files.
        testzip::Entry link = file(u"link"_s, "target");
        link.unixMode = 0120777;
        QCOMPARE(validate({file(u"ok.txt"_s), link}).problem, P::UnsupportedEntry);
        testzip::Entry device = file(u"dev"_s, "");
        device.unixMode = 020644;
        QCOMPARE(validate({device}).problem, P::UnsupportedEntry);

        // The start file.
        QCOMPARE(validate({file(u"a.txt"_s)}, {}, u"App.exe"_s).problem, P::MissingExecutable);
        QCOMPARE(validate({testzip::folder(u"App.exe/"_s), file(u"b.txt"_s)}, {}, u"App.exe"_s).problem,
            P::MissingExecutable);
        QCOMPARE(validate({file(u"sub/App.exe"_s)}, {}, u"App.exe"_s).problem, P::MissingExecutable);
    }

    void validator_limits()
    {
        using P = validator::Problem;
        const auto file = [](const QString& name, const QByteArray& data = "x") {
            return testzip::file(name, data);
        };

        validator::Limits limits;
        limits.maxEntries = 3;
        QCOMPARE(validate({file(u"a"_s), file(u"b"_s), file(u"c"_s)}, limits).problem, P::None);
        QCOMPARE(validate({file(u"a"_s), file(u"b"_s), file(u"c"_s), file(u"d"_s)}, limits).problem,
            P::TooManyEntries);

        limits = {};
        limits.maxEntrySize = 100;
        QCOMPARE(validate({file(u"a"_s, QByteArray(100, 'x'))}, limits).problem, P::None);
        QCOMPARE(validate({file(u"a"_s, QByteArray(101, 'x'))}, limits).problem, P::TooLarge);

        limits = {};
        limits.maxTotalSize = 250;
        QCOMPARE(validate({file(u"a"_s, QByteArray(100, 'x')), file(u"b"_s, QByteArray(100, 'y'))}, limits)
                     .problem,
            P::None);
        QCOMPARE(validate({file(u"a"_s, QByteArray(100, 'x')), file(u"b"_s, QByteArray(100, 'y')),
                              file(u"c"_s, QByteArray(100, 'z'))},
                     limits)
                     .problem,
            P::TooLarge);

        // A declared size is what counts, however small the compressed data is.
        testzip::Entry liar = file(u"a"_s, "x");
        liar.size = 3'000'000'000u;
        limits = {};
        QCOMPARE(validate({liar}, limits).problem, P::TooLarge);

        limits = {};
        limits.maxPathLength = 20;
        QCOMPARE(validate({file(u"a/b/c/d/e/f/g/h/i/j/k.txt"_s)}, limits).problem, P::UnsafePath);
        limits = {};
        limits.maxDepth = 2;
        QCOMPARE(validate({file(u"a/b/c.txt"_s)}, limits).problem, P::None);
        QCOMPARE(validate({file(u"a/b/c/d.txt"_s)}, limits).problem, P::UnsafePath);
    }

    void validator_compressionBomb()
    {
        // 20 MB of zeros in a few KB: far beyond a plausible ratio.
        const auto bomb = validate({testzip::file(u"zeros.bin"_s, QByteArray(20'000'000, '\0'))});
        QCOMPARE(bomb.problem, validator::Problem::CompressionBomb);

        // Small files may compress extremely well without being a bomb.
        QVERIFY(validate({testzip::file(u"zeros.bin"_s, QByteArray(1'000'000, '\0'))}).ok());

        // Ordinary compressible data passes.
        QByteArray text;
        for (int i = 0; i < 150'000; ++i)
            text += "line " + QByteArray::number(i) + ": lorem ipsum dolor sit amet\n";
        QVERIFY(text.size() > 4'000'000);
        QVERIFY(validate({testzip::file(u"text.txt"_s, text)}).ok());
    }

    void validator_failureLeavesNoItems()
    {
        const auto result = validate({testzip::file(u"ok.txt"_s, "x"), testzip::file(u"../evil.txt"_s, "x")});
        QVERIFY(!result.ok());
        QVERIFY(result.items.isEmpty());
        QVERIFY(result.detail.contains(u"evil"_s));
    }

    // --- extractor ------------------------------------------------------------

    void extractor_writesFilesAndFolders()
    {
        const QByteArray exe = payload(150'000);
        const auto opened = open(testzip::build({
            testzip::folder(u"empty/"_s),
            testzip::file(u"bin/App.exe"_s, exe),
            testzip::file(u"data/sub/config.json"_s, "{\"a\":1}", false),
        }));
        QVERIFY(opened.ok());
        const auto validation = validator::validate(*opened.archive, {}, u"bin/App.exe"_s);
        QVERIFY(validation.ok());

        QTemporaryDir target;
        qint64 lastDone = 0;
        qint64 total = 0;
        const auto result = extractor::extract(
            *opened.archive, validation, target.path(), nullptr, [&](qint64 done, qint64 all) {
                lastDone = done;
                total = all;
            });
        QVERIFY2(result.ok(), qPrintable(result.detail));
        QCOMPARE(lastDone, validation.totalSize);
        QCOMPARE(total, validation.totalSize);

        QCOMPARE(readFile(target.filePath(u"bin/App.exe"_s)), exe);
        QCOMPARE(readFile(target.filePath(u"data/sub/config.json"_s)), QByteArray("{\"a\":1}"));
        QVERIFY(QFileInfo(target.filePath(u"empty"_s)).isDir());
    }

    void extractor_neverOverwrites()
    {
        const auto opened = open(testzip::build({testzip::file(u"a.txt"_s, "new")}));
        QVERIFY(opened.ok());
        const auto validation = validator::validate(*opened.archive, {}, {});

        QTemporaryDir target;
        QVERIFY(writeFile(target.filePath(u"a.txt"_s), "existing"));
        const auto result = extractor::extract(*opened.archive, validation, target.path());
        QCOMPARE(result.status, extractor::Status::FileError);
        QCOMPARE(readFile(target.filePath(u"a.txt"_s)), QByteArray("existing"));
    }

    void extractor_missingTarget()
    {
        const auto opened = open(testzip::build({testzip::file(u"a.txt"_s, "x")}));
        const auto validation = validator::validate(*opened.archive, {}, {});
        QCOMPARE(extractor::extract(*opened.archive, validation, path(u"no-such-folder"_s)).status,
            extractor::Status::UnsafeTarget);
    }

    void extractor_refusesItemsOutsideTheFolder()
    {
        // The validator would never produce such an item. The extractor does not
        // rely on that: it checks every target path again.
        const auto opened = open(testzip::build({testzip::file(u"a.txt"_s, "x")}));
        validator::ValidationResult forged;
        forged.items.append({u"../escaped.txt"_s, false, 1, 0});
        forged.totalSize = 1;

        QTemporaryDir outer;
        QVERIFY(QDir().mkpath(outer.filePath(u"inner"_s)));
        const auto result = extractor::extract(*opened.archive, forged, outer.filePath(u"inner"_s));
        QCOMPARE(result.status, extractor::Status::UnsafeTarget);
        QVERIFY(!QFileInfo::exists(outer.filePath(u"escaped.txt"_s)));
    }

    void extractor_zipSlipWritesNothing()
    {
        // The whole chain: the archive tries to write above the target.
        QTemporaryDir outer;
        QVERIFY(QDir().mkpath(outer.filePath(u"staging"_s)));
        const auto opened = open(testzip::build({
            testzip::file(u"fine.txt"_s, "x"),
            testzip::file(u"../escaped.txt"_s, "evil"),
            testzip::file(u"..\\escaped2.txt"_s, "evil"),
        }));
        QVERIFY(opened.ok());
        const auto validation = validator::validate(*opened.archive, {}, {});
        QCOMPARE(validation.problem, validator::Problem::UnsafePath);
        QVERIFY(validation.items.isEmpty());

        // Even if a caller ignored the verdict, an empty item list writes nothing.
        const auto result = extractor::extract(*opened.archive, validation, outer.filePath(u"staging"_s));
        QVERIFY(result.ok());
        QVERIFY(QDir(outer.filePath(u"staging"_s)).isEmpty());
        QVERIFY(!QFileInfo::exists(outer.filePath(u"escaped.txt"_s)));
        QVERIFY(!QFileInfo::exists(outer.filePath(u"escaped2.txt"_s)));
    }

    void extractor_reportsDamagedData()
    {
        testzip::Entry bad = testzip::file(u"bad.bin"_s, payload(5000));
        bad.crc = 1;
        const auto opened = open(testzip::build({testzip::file(u"good.bin"_s, "ok"), bad}));
        QVERIFY(opened.ok());
        const auto validation = validator::validate(*opened.archive, {}, {});
        QVERIFY(validation.ok());

        QTemporaryDir target;
        QCOMPARE(extractor::extract(*opened.archive, validation, target.path()).status,
            extractor::Status::DataError);
    }

    void extractor_cancel()
    {
        const auto opened = open(testzip::build({
            testzip::file(u"a.bin"_s, payload(1000)),
            testzip::file(u"b.bin"_s, payload(1000)),
            testzip::file(u"c.bin"_s, payload(1000)),
        }));
        QVERIFY(opened.ok());
        const auto validation = validator::validate(*opened.archive, {}, {});

        std::atomic<bool> cancel = false;
        QTemporaryDir target;
        const auto result = extractor::extract(*opened.archive, validation, target.path(), &cancel,
            [&](qint64, qint64) { cancel = true; }); // cancel as soon as the first bytes arrive
        QCOMPARE(result.status, extractor::Status::Cancelled);
        QVERIFY(!QFileInfo::exists(target.filePath(u"c.bin"_s)));
    }
};

QTEST_GUILESS_MAIN(SecurityTests)
#include "SecurityTests.moc"
