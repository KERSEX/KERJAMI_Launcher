#include "security/ZipArchive.h"

#include <QByteArrayView>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <optional>

#include <zlib.h>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

constexpr quint32 kEndSignature = 0x06054b50;
constexpr quint32 kCentralSignature = 0x02014b50;
constexpr quint32 kLocalSignature = 0x04034b50;

constexpr int kEndSize = 22;
constexpr int kCentralSize = 46;
constexpr int kLocalSize = 30;
constexpr qint64 kMaxComment = 65535;

constexpr quint16 kFlagEncrypted = 0x0001;
constexpr quint16 kFlagDataDescriptor = 0x0008;
constexpr quint16 kFlagStrongEncryption = 0x0040;
constexpr quint16 kFlagUtf8 = 0x0800;
constexpr quint16 kFlagMaskedHeaders = 0x2000;

constexpr quint32 kZip64Marker32 = 0xFFFFFFFFu;
constexpr quint16 kZip64Marker16 = 0xFFFFu;

constexpr int kChunk = 64 * 1024;

quint16 u16(const QByteArray& data, qsizetype offset)
{
    return qFromLittleEndian<quint16>(data.constData() + offset);
}

quint32 u32(const QByteArray& data, qsizetype offset)
{
    return qFromLittleEndian<quint32>(data.constData() + offset);
}

ZipArchive::OpenResult failure(ZipArchive::Error error, const QString& detail)
{
    ZipArchive::OpenResult result;
    result.error = error;
    result.detail = detail;
    return result;
}

/// How the archiver marked the entry. The Unix mode in the upper half of the
/// external attributes is the only place a ZIP can say "symbolic link".
ZipArchive::EntryKind kindFromAttributes(
    quint16 versionMadeBy, quint32 externalAttributes, bool nameIsDirectory)
{
    const quint16 host = versionMadeBy >> 8;
    if (host == 3) { // Unix
        const quint32 type = (externalAttributes >> 16) & 0xF000u;
        switch (type) {
        case 0x8000u:
            return ZipArchive::EntryKind::File;
        case 0x4000u:
            return ZipArchive::EntryKind::Directory;
        case 0xA000u:
            return ZipArchive::EntryKind::Symlink;
        case 0u:
            break; // not specified
        default:
            return ZipArchive::EntryKind::Other;
        }
    } else if (host == 0 || host == 10) { // MS-DOS, NTFS
        if ((externalAttributes & 0x10u) != 0)
            return ZipArchive::EntryKind::Directory;
    }
    return nameIsDirectory ? ZipArchive::EntryKind::Directory : ZipArchive::EntryKind::File;
}

} // namespace

ZipArchive::OpenResult ZipArchive::open(const QString& path)
{
    std::unique_ptr<ZipArchive> archive(new ZipArchive);
    QFile& file = archive->m_file;
    file.setFileName(path);
    if (!file.open(QIODevice::ReadOnly))
        return failure(Error::CannotOpen, file.errorString());

    const qint64 fileSize = file.size();
    if (fileSize < kEndSize)
        return failure(Error::NotAZip, u"file is shorter than an end-of-archive record"_s);

    // --- end of central directory record -------------------------------------
    const qint64 tailSize = qMin<qint64>(fileSize, kEndSize + kMaxComment);
    if (!file.seek(fileSize - tailSize))
        return failure(Error::CannotOpen, file.errorString());
    const QByteArray tail = file.read(tailSize);
    if (tail.size() != tailSize)
        return failure(Error::CannotOpen, u"read error at the end of the file"_s);

    // The record is the last one whose comment ends exactly at the end of the file.
    qsizetype endPos = -1;
    for (qsizetype pos = tail.size() - kEndSize; pos >= 0; --pos) {
        if (u32(tail, pos) == kEndSignature && pos + kEndSize + u16(tail, pos + 20) == tail.size()) {
            endPos = pos;
            break;
        }
    }
    if (endPos < 0)
        return failure(Error::NotAZip, u"no end-of-archive record"_s);

    const quint16 diskNumber = u16(tail, endPos + 4);
    const quint16 directoryDisk = u16(tail, endPos + 6);
    const quint16 entriesOnDisk = u16(tail, endPos + 8);
    const quint16 entryCount = u16(tail, endPos + 10);
    const quint32 directorySize = u32(tail, endPos + 12);
    const quint32 directoryOffset = u32(tail, endPos + 16);

    if (diskNumber != 0 || directoryDisk != 0)
        return failure(Error::Unsupported, u"archive spans several disks"_s);
    if (entryCount == kZip64Marker16 || entriesOnDisk == kZip64Marker16 || directorySize == kZip64Marker32
        || directoryOffset == kZip64Marker32)
        return failure(Error::Unsupported, u"ZIP64 archives are not supported"_s);
    if (entriesOnDisk != entryCount)
        return failure(Error::Corrupt, u"entry counts of the end record disagree"_s);

    const qint64 endOffset = fileSize - tailSize + endPos;
    if (qint64(directoryOffset) + qint64(directorySize) != endOffset)
        return failure(Error::Corrupt, u"central directory does not end at the end record"_s);
    if (directorySize > kMaxDirectoryBytes)
        return failure(Error::Unsupported, u"central directory is too large"_s);

    // --- central directory ---------------------------------------------------
    if (!file.seek(directoryOffset))
        return failure(Error::CannotOpen, file.errorString());
    const QByteArray directory = file.read(directorySize);
    if (directory.size() != qsizetype(directorySize))
        return failure(Error::CannotOpen, u"read error in the central directory"_s);

    struct Range {
        qint64 begin;
        qint64 end;
    };
    QList<Range> ranges;
    ranges.reserve(entryCount);
    archive->m_entries.reserve(entryCount);

    qsizetype cursor = 0;
    for (int index = 0; index < entryCount; ++index) {
        if (cursor + kCentralSize > directory.size() || u32(directory, cursor) != kCentralSignature)
            return failure(Error::Corrupt, u"damaged central directory entry %1"_s.arg(index));

        const quint16 versionMadeBy = u16(directory, cursor + 4);
        const quint16 flags = u16(directory, cursor + 8);
        const quint16 method = u16(directory, cursor + 10);
        const quint32 crc = u32(directory, cursor + 16);
        const quint32 compressedSize = u32(directory, cursor + 20);
        const quint32 size = u32(directory, cursor + 24);
        const quint16 nameLength = u16(directory, cursor + 28);
        const quint16 extraLength = u16(directory, cursor + 30);
        const quint16 commentLength = u16(directory, cursor + 32);
        const quint16 diskStart = u16(directory, cursor + 34);
        const quint32 externalAttributes = u32(directory, cursor + 38);
        const quint32 localOffset = u32(directory, cursor + 42);

        const qsizetype recordEnd = cursor + kCentralSize + nameLength + extraLength + commentLength;
        if (recordEnd > directory.size())
            return failure(Error::Corrupt, u"central directory entry %1 is cut off"_s.arg(index));

        if ((flags & (kFlagEncrypted | kFlagStrongEncryption | kFlagMaskedHeaders)) != 0)
            return failure(Error::Unsupported, u"entry %1 is encrypted"_s.arg(index));
        if (method != 0 && method != 8)
            return failure(
                Error::Unsupported, u"entry %1 uses compression method %2"_s.arg(index).arg(method));
        if (diskStart != 0)
            return failure(Error::Unsupported, u"entry %1 lies on another disk"_s.arg(index));
        if (compressedSize == kZip64Marker32 || size == kZip64Marker32 || localOffset == kZip64Marker32)
            return failure(Error::Unsupported, u"entry %1 needs ZIP64"_s.arg(index));

        const QByteArray rawName = directory.mid(cursor + kCentralSize, nameLength);
        if (rawName.isEmpty() || rawName.contains('\0'))
            return failure(Error::Corrupt, u"entry %1 has an empty or invalid name"_s.arg(index));

        QString name;
        if ((flags & kFlagUtf8) != 0) {
            if (!QByteArrayView(rawName).isValidUtf8())
                return failure(Error::Corrupt, u"entry %1 has a name that is not valid UTF-8"_s.arg(index));
            name = QString::fromUtf8(rawName);
        } else {
            // Without the UTF-8 flag the encoding is whatever the archiver's
            // computer used. Guessing would let different tools see different
            // names for the same entry, so only plain ASCII is accepted.
            const bool ascii =
                std::all_of(rawName.cbegin(), rawName.cend(), [](char c) { return uchar(c) < 0x80; });
            if (!ascii)
                return failure(
                    Error::Unsupported, u"entry %1 has a name in an unknown encoding"_s.arg(index));
            name = QString::fromLatin1(rawName);
        }

        const bool nameIsDirectory = name.endsWith(u'/') || name.endsWith(u'\\');
        Entry entry;
        entry.name = name;
        entry.kind = kindFromAttributes(versionMadeBy, externalAttributes, nameIsDirectory);
        entry.method = method;
        entry.compressedSize = compressedSize;
        entry.size = size;
        entry.crc = crc;

        // A name and the type the archiver recorded must tell the same story.
        if (nameIsDirectory && entry.kind == EntryKind::File)
            return failure(Error::Corrupt, u"entry %1 is a directory name marked as a file"_s.arg(index));
        if (!nameIsDirectory && entry.kind == EntryKind::Directory)
            return failure(Error::Corrupt, u"entry %1 is a file name marked as a directory"_s.arg(index));
        if (entry.kind == EntryKind::Directory && (size != 0 || compressedSize != 0))
            return failure(Error::Corrupt, u"directory entry %1 carries data"_s.arg(index));
        if (method == 0 && compressedSize != size)
            return failure(Error::Corrupt, u"stored entry %1 has two different sizes"_s.arg(index));

        // --- local header: must agree with the central directory -------------
        if (!file.seek(localOffset))
            return failure(Error::Corrupt, u"entry %1 starts outside the file"_s.arg(index));
        const QByteArray local = file.read(kLocalSize + nameLength);
        if (local.size() != kLocalSize + nameLength || u32(local, 0) != kLocalSignature)
            return failure(Error::Corrupt, u"entry %1 has no local header"_s.arg(index));

        const quint16 localFlags = u16(local, 6);
        const quint16 localNameLength = u16(local, 26);
        const quint16 localExtraLength = u16(local, 28);
        if ((localFlags & (kFlagEncrypted | kFlagStrongEncryption)) != 0)
            return failure(Error::Unsupported, u"entry %1 is encrypted"_s.arg(index));
        if (u16(local, 8) != method || localNameLength != nameLength
            || local.mid(kLocalSize, nameLength) != rawName)
            return failure(
                Error::Corrupt, u"local header of entry %1 contradicts the directory"_s.arg(index));
        // Sizes and CRC are only in the local header when no data descriptor follows.
        if ((localFlags & kFlagDataDescriptor) == 0
            && (u32(local, 14) != crc || u32(local, 18) != compressedSize || u32(local, 22) != size))
            return failure(Error::Corrupt, u"local header of entry %1 has other sizes"_s.arg(index));

        entry.dataOffset = qint64(localOffset) + kLocalSize + nameLength + localExtraLength;
        const qint64 dataEnd = entry.dataOffset + compressedSize;
        if (dataEnd > directoryOffset)
            return failure(
                Error::Corrupt, u"data of entry %1 reaches into the central directory"_s.arg(index));

        ranges.append({qint64(localOffset), dataEnd});
        archive->m_entries.append(std::move(entry));
        cursor = recordEnd;
    }
    if (cursor != directory.size())
        return failure(Error::Corrupt, u"unused bytes after the last directory entry"_s);

    // Overlapping entries are how "zip quines" and some bombs are built: one
    // stretch of compressed data counted for many files.
    std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) { return a.begin < b.begin; });
    for (qsizetype i = 1; i < ranges.size(); ++i) {
        if (ranges.at(i).begin < ranges.at(i - 1).end)
            return failure(Error::Corrupt, u"entries overlap in the archive"_s);
    }

    OpenResult result;
    result.archive = std::move(archive);
    return result;
}

ZipArchive::ExtractResult ZipArchive::extract(
    const Entry& entry, QIODevice& out, const std::atomic<bool>* cancel)
{
    const auto fail = [](ExtractStatus status, const QString& detail) {
        ExtractResult result;
        result.status = status;
        result.detail = detail;
        return result;
    };
    const auto cancelled = [cancel] { return cancel != nullptr && cancel->load(); };

    if (entry.kind != EntryKind::File)
        return fail(ExtractStatus::CorruptData, u"only files can be extracted"_s);
    if (!m_file.seek(entry.dataOffset))
        return fail(ExtractStatus::ReadError, m_file.errorString());

    std::array<char, kChunk> input;
    std::array<char, kChunk> output;
    uLong crc = ::crc32(0L, Z_NULL, 0);
    qint64 produced = 0;
    qint64 remainingInput = entry.compressedSize;

    const auto deliver = [&](qint64 count) -> std::optional<ExtractResult> {
        if (produced + count > qint64(entry.size))
            return fail(ExtractStatus::SizeMismatch, u"data is longer than the entry says"_s);
        if (out.write(output.data(), count) != count)
            return fail(ExtractStatus::WriteError, out.errorString());
        crc = ::crc32(crc, reinterpret_cast<const Bytef*>(output.data()), uInt(count));
        produced += count;
        return std::nullopt;
    };

    if (entry.method == 0) {
        while (remainingInput > 0) {
            if (cancelled())
                return fail(ExtractStatus::Cancelled, {});
            const qint64 want = qMin<qint64>(remainingInput, kChunk);
            const qint64 got = m_file.read(output.data(), want);
            if (got <= 0)
                return fail(ExtractStatus::ReadError, u"archive is cut off"_s);
            remainingInput -= got;
            if (auto failed = deliver(got))
                return *failed;
        }
    } else {
        z_stream stream {};
        if (inflateInit2(&stream, -15) != Z_OK)
            return fail(ExtractStatus::CorruptData, u"zlib could not be initialised"_s);

        int status = Z_OK;
        std::optional<ExtractResult> failed;
        while (status != Z_STREAM_END && !failed) {
            if (cancelled()) {
                failed = fail(ExtractStatus::Cancelled, {});
                break;
            }
            if (stream.avail_in == 0) {
                if (remainingInput == 0) {
                    failed = fail(ExtractStatus::CorruptData, u"compressed data ends too early"_s);
                    break;
                }
                const qint64 got = m_file.read(input.data(), qMin<qint64>(remainingInput, kChunk));
                if (got <= 0) {
                    failed = fail(ExtractStatus::ReadError, u"archive is cut off"_s);
                    break;
                }
                remainingInput -= got;
                stream.next_in = reinterpret_cast<Bytef*>(input.data());
                stream.avail_in = uInt(got);
            }

            const uInt inputBefore = stream.avail_in;
            stream.next_out = reinterpret_cast<Bytef*>(output.data());
            stream.avail_out = kChunk;
            status = inflate(&stream, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
                failed = fail(ExtractStatus::CorruptData, u"zlib error %1"_s.arg(status));
                break;
            }
            const qint64 count = kChunk - stream.avail_out;
            if (count > 0) {
                failed = deliver(count);
                if (failed)
                    break;
            } else if (stream.avail_in == inputBefore) {
                failed = fail(ExtractStatus::CorruptData, u"decompression made no progress"_s);
                break;
            }
        }

        const bool trailing = stream.avail_in != 0 || remainingInput != 0;
        inflateEnd(&stream);
        if (failed)
            return *failed;
        if (trailing)
            return fail(ExtractStatus::SizeMismatch, u"compressed data is longer than the entry says"_s);
    }

    if (produced != qint64(entry.size))
        return fail(ExtractStatus::SizeMismatch, u"data is shorter than the entry says"_s);
    if (quint32(crc) != entry.crc)
        return fail(ExtractStatus::CrcMismatch, u"checksum of the data does not match"_s);
    return {};
}

} // namespace launcher
