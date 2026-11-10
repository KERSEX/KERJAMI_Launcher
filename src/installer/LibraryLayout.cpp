#include "installer/LibraryLayout.h"

#include "installer/FileOps.h"
#include "installer/InstalledProjects.h"

#include <QDir>

using namespace Qt::StringLiterals;

namespace launcher {

LibraryLayout::LibraryLayout(const QString& libraryDir, const QString& tempDir)
    : m_libraryDir(QDir::cleanPath(libraryDir))
    , m_tempDir(QDir::cleanPath(tempDir))
{
}

QString LibraryLayout::projectDir(const QString& installDirectory) const
{
    return m_libraryDir + u'/' + installDirectory;
}

QString LibraryLayout::currentDir(const QString& installDirectory) const
{
    return projectDir(installDirectory) + u"/current"_s;
}

QString LibraryLayout::backupsDir(const QString& installDirectory) const
{
    return projectDir(installDirectory) + u"/backups"_s;
}

QString LibraryLayout::stagingDir(const QString& projectId) const
{
    return m_tempDir + u'/' + QLatin1StringView(kStagingPrefix) + projectId;
}

QString LibraryLayout::trashDir(const QString& projectId) const
{
    return m_tempDir + u'/' + QLatin1StringView(kTrashPrefix) + projectId;
}

QString LibraryLayout::executablePath(const InstalledProject& project) const
{
    return currentDir(project.installDirectory) + u'/' + project.executable;
}

int LibraryLayout::cleanupTemp() const
{
    int removed = 0;
    const QDir dir(m_tempDir);
    const QStringList names = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QString& name : names) {
        const bool ours = name.startsWith(QLatin1StringView(kStagingPrefix))
            || name.startsWith(QLatin1StringView(kTrashPrefix));
        if (ours && FileOps::removeTree(dir.filePath(name)))
            ++removed;
    }
    return removed;
}

} // namespace launcher
