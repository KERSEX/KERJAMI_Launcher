#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtEndian>

#include <zlib.h>

#include <optional>

/// Builds ZIP archives for tests, including broken and hostile ones.
///
/// Every field the reader checks can be overridden, so each safety rule has a
/// test that violates exactly that rule.
namespace testzip {

struct Entry {
    QByteArray name; ///< Raw name bytes as stored.
    QByteArray data;
    bool deflate = true;
    bool utf8Flag = false;
    bool directory = false;

    quint16 hostSystem = 3;             ///< 3 = Unix, 0 = MS-DOS.
    quint32 unixMode = 0;               ///< 0 = pick 0100644 or 040755 by type.
    std::optional<quint32> crc;         ///< Overrides the CRC-32 in both headers.
    std::optional<quint32> size;        ///< Overrides the uncompressed size in both headers.
    std::optional<quint16> flags;       ///< Overrides the general purpose flags in the central directory.
    std::optional<quint16> method;      ///< Overrides the method in the central directory only.
    std::optional<quint32> localOffset; ///< Overrides the offset in the central directory.
    QByteArray localName;               ///< If set, the name in the local header.
    std::optional<QByteArray> rawCompressed; ///< Stored instead of the real compressed data.
};

inline Entry file(const QString& name, const QByteArray& data, bool deflate = true)
{
    Entry entry;
    entry.name = name.toUtf8();
    entry.data = data;
    entry.deflate = deflate;
    entry.utf8Flag = true;
    return entry;
}

inline Entry folder(const QString& name)
{
    Entry entry;
    entry.name = name.toUtf8();
    entry.directory = true;
    entry.utf8Flag = true;
    return entry;
}

inline QByteArray deflateRaw(const QByteArray& data)
{
    z_stream stream {};
    deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    QByteArray out(int(deflateBound(&stream, uLong(data.size()))) + 16, Qt::Uninitialized);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    stream.avail_in = uInt(data.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = uInt(out.size());
    deflate(&stream, Z_FINISH);
    out.truncate(int(stream.total_out));
    deflateEnd(&stream);
    return out;
}

inline void put16(QByteArray& out, quint16 value)
{
    char bytes[2];
    qToLittleEndian(value, bytes);
    out.append(bytes, 2);
}

inline void put32(QByteArray& out, quint32 value)
{
    char bytes[4];
    qToLittleEndian(value, bytes);
    out.append(bytes, 4);
}

inline quint32 crcOf(const QByteArray& data)
{
    uLong crc = ::crc32(0L, Z_NULL, 0);
    crc = ::crc32(crc, reinterpret_cast<const Bytef*>(data.constData()), uInt(data.size()));
    return quint32(crc);
}

inline QByteArray build(const QList<Entry>& entries, const QByteArray& comment = {})
{
    QByteArray archive;
    QByteArray directory;

    for (const Entry& entry : entries) {
        const quint32 offset = quint32(archive.size());
        const quint16 method = entry.directory || !entry.deflate ? 0 : 8;
        const QByteArray compressed =
            entry.rawCompressed ? *entry.rawCompressed : (method == 8 ? deflateRaw(entry.data) : entry.data);
        const quint32 crc = entry.crc.value_or(entry.directory ? 0 : crcOf(entry.data));
        const quint32 size = entry.size.value_or(entry.directory ? 0 : quint32(entry.data.size()));
        const quint32 compressedSize = entry.directory ? 0 : quint32(compressed.size());
        const quint16 flags = quint16(entry.utf8Flag ? 0x0800 : 0);
        const QByteArray localName = entry.localName.isEmpty() ? entry.name : entry.localName;

        // local header
        put32(archive, 0x04034b50);
        put16(archive, 20);
        put16(archive, flags);
        put16(archive, method);
        put16(archive, 0);    // time
        put16(archive, 0x21); // date
        put32(archive, crc);
        put32(archive, compressedSize);
        put32(archive, size);
        put16(archive, quint16(localName.size()));
        put16(archive, 0);
        archive.append(localName);
        if (!entry.directory)
            archive.append(compressed);

        // central directory entry
        quint32 mode = entry.unixMode;
        if (mode == 0)
            mode = entry.directory ? 040755u : 0100644u;
        const quint32 externalAttributes =
            entry.hostSystem == 3 ? (mode << 16) : (entry.directory ? 0x10u : 0u);
        put32(directory, 0x02014b50);
        put16(directory, quint16((entry.hostSystem << 8) | 20));
        put16(directory, 20);
        put16(directory, entry.flags.value_or(flags));
        put16(directory, entry.method.value_or(method));
        put16(directory, 0);
        put16(directory, 0x21);
        put32(directory, crc);
        put32(directory, compressedSize);
        put32(directory, size);
        put16(directory, quint16(entry.name.size()));
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put32(directory, externalAttributes);
        put32(directory, entry.localOffset.value_or(offset));
        directory.append(entry.name);
    }

    const quint32 directoryOffset = quint32(archive.size());
    archive.append(directory);

    put32(archive, 0x06054b50);
    put16(archive, 0);
    put16(archive, 0);
    put16(archive, quint16(entries.size()));
    put16(archive, quint16(entries.size()));
    put32(archive, quint32(directory.size()));
    put32(archive, directoryOffset);
    put16(archive, quint16(comment.size()));
    archive.append(comment);
    return archive;
}

} // namespace testzip
