#pragma once

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QString>

#include <atomic>
#include <memory>

namespace launcher {

/// Reads ZIP archives, strictly.
///
/// The archive comes from the internet, so every number in it is treated as
/// hostile. The reader understands the plain ZIP format with stored and
/// deflated entries, which is what every tool produces for a few hundred
/// megabytes of data. It refuses everything else instead of guessing: ZIP64,
/// encryption, other compression methods, entries that overlap or point
/// outside the file, local headers that contradict the central directory, and
/// names that are not valid text.
///
/// Reading the structure (open) is separate from judging the names and sizes
/// (ArchiveValidator) and from writing files (ArchiveExtractor). This class
/// never writes to the file system.
class ZipArchive {
public:
    enum class Error {
        None,
        CannotOpen,  ///< The file does not exist or cannot be read.
        NotAZip,     ///< No end-of-archive record: not a ZIP file, or cut off.
        Unsupported, ///< Valid ZIP features this reader does not handle (ZIP64, encryption, ...).
        Corrupt,     ///< The structure contradicts itself.
    };

    enum class EntryKind {
        File,
        Directory,
        Symlink, ///< Marked as a symbolic link by the archiver; never extracted.
        Other,   ///< Device, pipe or another special file; never extracted.
    };

    struct Entry {
        QString name; ///< As stored, still unchecked. Directories end in "/".
        EntryKind kind = EntryKind::File;
        quint16 method = 0; ///< 0 = stored, 8 = deflated.
        quint32 compressedSize = 0;
        quint32 size = 0;
        quint32 crc = 0;       ///< CRC-32 of the uncompressed data.
        qint64 dataOffset = 0; ///< Where the compressed data starts in the archive file.
    };

    struct OpenResult {
        Error error = Error::None;
        QString detail; ///< Technical detail for the log.
        std::unique_ptr<ZipArchive> archive;

        bool ok() const { return error == Error::None; }
    };

    /// Largest central directory the reader accepts.
    static constexpr qint64 kMaxDirectoryBytes = 16LL * 1024 * 1024;

    static OpenResult open(const QString& path);

    const QList<Entry>& entries() const { return m_entries; }

    enum class ExtractStatus {
        Ok,
        Cancelled,
        ReadError,    ///< The archive file could not be read.
        WriteError,   ///< The target device refused the data.
        CorruptData,  ///< The compressed data is damaged.
        SizeMismatch, ///< The data is shorter or longer than the entry says.
        CrcMismatch,
    };

    struct ExtractResult {
        ExtractStatus status = ExtractStatus::Ok;
        QString detail;

        bool ok() const { return status == ExtractStatus::Ok; }
    };

    /// Decompresses one entry into @p out. Never writes more than the entry's
    /// declared size, so a compression bomb cannot fill the disk, and checks the
    /// CRC-32 at the end. The caller decides what happens with partial output
    /// after an error.
    ExtractResult extract(const Entry& entry, QIODevice& out, const std::atomic<bool>* cancel = nullptr);

private:
    ZipArchive() = default;

    QFile m_file;
    QList<Entry> m_entries;
};

} // namespace launcher
