#include "installer/FileOps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

using namespace Qt::StringLiterals;

namespace launcher::FileOps {

namespace {

bool removeFile(const QString& path)
{
    if (QFile::remove(path))
        return true;
    // A read-only attribute blocks deletion on Windows.
    QFile::setPermissions(path,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser | QFileDevice::WriteUser);
    return QFile::remove(path);
}

bool removeEmptyDirectory(const QString& path)
{
    return QDir().rmdir(path);
}

} // namespace

bool isLink(const QString& path)
{
    const QFileInfo info(path);
    return info.isSymLink() || info.isJunction();
}

bool removeTree(const QString& path, QString* error)
{
    if (error != nullptr)
        error->clear();
    const auto fail = [error](const QString& failedPath) {
        if (error != nullptr && error->isEmpty())
            *error = failedPath;
        return false;
    };

    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return true;

    // A link is removed itself. For a link to a folder that means rmdir, not a
    // recursive delete of the target.
    if (isLink(path)) {
        const bool done = info.isDir() ? removeEmptyDirectory(path) : removeFile(path);
        return done || fail(path);
    }
    if (!info.isDir())
        return removeFile(path) || fail(path);

    bool allGone = true;
    const QDir dir(path);
    const QFileInfoList entries =
        dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo& entry : entries) {
        if (!removeTree(entry.filePath(), error))
            allGone = false;
    }
    if (!allGone)
        return false;
    return removeEmptyDirectory(path) || fail(path);
}

bool copyTree(const QString& from, const QString& to, QString* error)
{
    const auto fail = [error](const QString& what) {
        if (error)
            *error = what;
        return false;
    };
    if (isLink(from))
        return true; // a link is not data of the project, and not ours to follow

    const QFileInfo source(from);
    const QFileInfo target(to);
    if (source.isDir()) {
        if (target.exists() || target.isSymLink()) {
            if (!target.isDir() || isLink(to)) {
                QString removeError;
                if (!removeTree(to, &removeError))
                    return fail(removeError);
            }
        }
        if (!QDir().mkpath(to))
            return fail(u"the folder %1 cannot be created"_s.arg(QDir::toNativeSeparators(to)));
        const QFileInfoList entries =
            QDir(from).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        for (const QFileInfo& entry : entries) {
            if (!copyTree(entry.filePath(), to + u'/' + entry.fileName(), error))
                return false;
        }
        return true;
    }

    if (target.exists() || target.isSymLink()) {
        QString removeError;
        const bool gone = target.isDir() || isLink(to) ? removeTree(to, &removeError) : removeFile(to);
        if (!gone)
            return fail(removeError.isEmpty() ? u"%1 cannot be replaced"_s.arg(QDir::toNativeSeparators(to))
                                              : removeError);
    }
    if (!QDir().mkpath(target.absolutePath()))
        return fail(u"the folder of %1 cannot be created"_s.arg(QDir::toNativeSeparators(to)));
    if (!QFile::copy(from, to))
        return fail(u"%1 cannot be copied"_s.arg(QDir::toNativeSeparators(from)));
    return true;
}

bool renameWithRetry(const QString& from, const QString& to, QString* error)
{
    constexpr int kAttempts = 5;
    constexpr unsigned long kPauseMs = 200;

    for (int attempt = 1; attempt <= kAttempts; ++attempt) {
        if (QDir().rename(from, to))
            return true;
        if (attempt < kAttempts)
            QThread::msleep(kPauseMs);
    }
    if (error != nullptr)
        *error = u"could not rename \"%1\" to \"%2\""_s.arg(from, to);
    return false;
}

} // namespace launcher::FileOps
