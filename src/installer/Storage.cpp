#include "installer/Storage.h"

#include "core/Logger.h"
#include "core/PathManager.h"
#include "installer/FileOps.h"

#include <QDir>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace launcher::Storage {

namespace {

constexpr QDir::Filters kAllEntries = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;

/// Bytes of all files below @p path. A link (a symbolic link or a directory junction) is not
/// entered: what it points to is somewhere else and none of this folder's business.
/// QDirIterator would walk into a junction, so the walk is done by hand.
qint64 treeSize(const QString& path, const std::atomic<bool>* cancel)
{
    qint64 total = 0;
    const QFileInfoList entries = QDir(path).entryInfoList(kAllEntries);
    for (const QFileInfo& entry : entries) {
        if (cancel && cancel->load())
            break;
        if (FileOps::isLink(entry.filePath()))
            continue;
        total += entry.isDir() ? treeSize(entry.filePath(), cancel) : entry.size();
    }
    return total;
}

/// Deletes the entries inside a folder, not the folder itself.
bool clearFolderContents(const QString& folder, QString* firstError)
{
    bool allGone = true;
    const QFileInfoList entries = QDir(folder).entryInfoList(kAllEntries);
    for (const QFileInfo& entry : entries) {
        QString error;
        if (!FileOps::removeTree(entry.filePath(), &error)) {
            allGone = false;
            if (firstError && firstError->isEmpty())
                *firstError = error;
        }
    }
    return allGone;
}

/// The backups folders of all projects: library/<project>/backups. A project folder that is a link is
/// skipped.
QStringList backupFolders(const PathManager& paths)
{
    QStringList result;
    const QFileInfoList projects = QDir(paths.libraryDir()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& project : projects) {
        if (FileOps::isLink(project.filePath()))
            continue;
        const QString backups = project.filePath() + u"/backups"_s;
        if (QFileInfo(backups).isDir() && !FileOps::isLink(backups))
            result.append(backups);
    }
    return result;
}

} // namespace

Usage measure(const PathManager& paths, const std::atomic<bool>* cancel)
{
    Usage usage;
    usage.downloadsAndTemp = treeSize(paths.downloadsDir(), cancel) + treeSize(paths.tempDir(), cancel);
    for (const QString& folder : backupFolders(paths))
        usage.backups += treeSize(folder, cancel);
    return usage;
}

bool clearDownloadsAndTemp(const PathManager& paths, QString* error)
{
    QString firstError;
    const bool downloads = clearFolderContents(paths.downloadsDir(), &firstError);
    const bool temp = clearFolderContents(paths.tempDir(), &firstError);
    if (error)
        *error = firstError;
    if (downloads && temp)
        qCInfo(lcInstall) << "Cleared downloads/ and temp/";
    else
        qCWarning(lcInstall).noquote() << "Clearing downloads/ and temp/ was incomplete:" << firstError;
    return downloads && temp;
}

bool clearBackups(const PathManager& paths, QString* error)
{
    QString firstError;
    bool all = true;
    for (const QString& folder : backupFolders(paths))
        all = clearFolderContents(folder, &firstError) && all;
    if (error)
        *error = firstError;
    if (all)
        qCInfo(lcInstall) << "Cleared the backups of earlier versions";
    else
        qCWarning(lcInstall).noquote() << "Clearing the backups was incomplete:" << firstError;
    return all;
}

} // namespace launcher::Storage
