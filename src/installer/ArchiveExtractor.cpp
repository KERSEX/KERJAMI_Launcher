#include "installer/ArchiveExtractor.h"

#include "core/PathManager.h"
#include "installer/FileOps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace launcher::ArchiveExtractor {

namespace {

Result failure(Status status, const QString& detail)
{
    Result result;
    result.status = status;
    result.detail = detail;
    return result;
}

/// A counting wrapper so progress follows the bytes that are really written.
class ProgressFile : public QFile {
public:
    ProgressFile(const QString& path, qint64& done, qint64 total, const Progress& progress)
        : QFile(path)
        , m_done(done)
        , m_total(total)
        , m_progress(progress)
    {
    }

protected:
    qint64 writeData(const char* data, qint64 size) override
    {
        const qint64 written = QFile::writeData(data, size);
        if (written > 0) {
            m_done += written;
            if (m_progress)
                m_progress(m_done, m_total);
        }
        return written;
    }

private:
    qint64& m_done;
    qint64 m_total;
    const Progress& m_progress;
};

} // namespace

Result extract(ZipArchive& archive, const ArchiveValidator::ValidationResult& validation,
    const QString& targetDir, const std::atomic<bool>* cancel, const Progress& progress)
{
    const QString root = QDir::cleanPath(targetDir);
    const QFileInfo rootInfo(root);
    if (!rootInfo.isDir() || FileOps::isLink(root))
        return failure(Status::UnsafeTarget, u"target folder \"%1\" is missing or a link"_s.arg(root));

    qint64 done = 0;
    for (const ArchiveValidator::Item& item : validation.items) {
        if (cancel != nullptr && cancel->load())
            return failure(Status::Cancelled, {});

        const QString target = QDir::cleanPath(root + u'/' + item.path);
        if (!PathManager::isInside(root, target))
            return failure(Status::UnsafeTarget, u"\"%1\" would leave the target folder"_s.arg(item.path));

        if (item.isDirectory) {
            if (!QDir().mkpath(target))
                return failure(Status::FileError, u"could not create folder \"%1\""_s.arg(item.path));
            continue;
        }

        // The folders above the file are created by hand, one by one, so that
        // an existing link (which a fresh, empty staging folder cannot contain,
        // but a race could plant) is noticed.
        const QString parent = QFileInfo(target).absolutePath();
        if (!PathManager::isInside(root, parent) && QDir::cleanPath(parent) != root)
            return failure(Status::UnsafeTarget, u"\"%1\" would leave the target folder"_s.arg(item.path));
        if (!QDir().mkpath(parent))
            return failure(Status::FileError, u"could not create the folder for \"%1\""_s.arg(item.path));
        for (QString dir = parent; dir != root && dir.size() > root.size();
            dir = QFileInfo(dir).absolutePath()) {
            if (FileOps::isLink(dir))
                return failure(Status::UnsafeTarget, u"\"%1\" lies below a link"_s.arg(item.path));
        }

        const ZipArchive::Entry& entry = archive.entries().at(item.entryIndex);
        ProgressFile file(target, done, validation.totalSize, progress);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return failure(
                Status::FileError, u"could not create \"%1\": %2"_s.arg(item.path, file.errorString()));

        const ZipArchive::ExtractResult data = archive.extract(entry, file, cancel);
        file.close();
        switch (data.status) {
        case ZipArchive::ExtractStatus::Ok:
            break;
        case ZipArchive::ExtractStatus::Cancelled:
            return failure(Status::Cancelled, {});
        case ZipArchive::ExtractStatus::WriteError:
            return failure(Status::FileError, u"could not write \"%1\": %2"_s.arg(item.path, data.detail));
        case ZipArchive::ExtractStatus::ReadError:
        case ZipArchive::ExtractStatus::CorruptData:
        case ZipArchive::ExtractStatus::SizeMismatch:
        case ZipArchive::ExtractStatus::CrcMismatch:
            return failure(Status::DataError, u"\"%1\": %2"_s.arg(item.path, data.detail));
        }
    }
    return {};
}

} // namespace launcher::ArchiveExtractor
