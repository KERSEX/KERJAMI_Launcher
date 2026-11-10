#include "core/PathManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

/// Absolute path with forward slashes and without "." or ".." segments.
QString normalized(const QString& path)
{
    if (path.isEmpty())
        return {};
    return QDir::cleanPath(QDir(path).absolutePath());
}

/// Base directory followed by exactly one slash ("D:/" stays "D:/").
QString withTrailingSlash(const QString& directory)
{
    return directory.endsWith(u'/') ? directory : directory + u'/';
}

/// Attribute checks are unreliable on Windows (ACLs, read-only media), so the
/// only trustworthy test is to actually create a file.
bool canCreateFileIn(const QString& directory)
{
    QTemporaryFile probe(directory + u"/.write-test-XXXXXX"_s);
    return probe.open();
}

} // namespace

PathManager PathManager::forApplication()
{
    return PathManager(QCoreApplication::applicationDirPath());
}

PathManager::PathManager(const QString& rootDir)
    : m_root(normalized(rootDir))
{
}

QString PathManager::configDir() const
{
    return m_root + u"/launcher"_s;
}

QString PathManager::libraryDir() const
{
    return m_root + u"/library"_s;
}

QString PathManager::downloadsDir() const
{
    return m_root + u"/downloads"_s;
}

QString PathManager::tempDir() const
{
    return m_root + u"/temp"_s;
}

QString PathManager::cacheDir() const
{
    return m_root + u"/cache"_s;
}

QString PathManager::logsDir() const
{
    return m_root + u"/logs"_s;
}

QString PathManager::updaterDir() const
{
    return m_root + u"/updater"_s;
}

QString PathManager::updateJournalFile() const
{
    return updaterDir() + u"/journal.json"_s;
}

QString PathManager::updateReportFile() const
{
    return updaterDir() + u"/last-update.json"_s;
}

QString PathManager::updaterLockFile() const
{
    return updaterDir() + u"/updater.lock"_s;
}

QString PathManager::updateBackupsDir() const
{
    return updaterDir() + u"/backups"_s;
}

QString PathManager::settingsFile() const
{
    return configDir() + u"/settings.json"_s;
}

QString PathManager::stateFile() const
{
    return configDir() + u"/launcher.json"_s;
}

QString PathManager::registryCacheFile() const
{
    return cacheDir() + u"/registry.json"_s;
}

QString PathManager::lockFile() const
{
    return configDir() + u"/launcher.lock"_s;
}

QStringList PathManager::dataDirectories() const
{
    return {configDir(), libraryDir(), downloadsDir(), tempDir(), cacheDir(), logsDir()};
}

PathManager::PrepareResult PathManager::prepare() const
{
    if (!QFileInfo(m_root).isDir())
        return {PrepareError::RootMissing, m_root};

    // The root itself must be writable too: the self-update replaces files there.
    if (!canCreateFileIn(m_root))
        return {PrepareError::NotWritable, m_root};

    const QStringList directories = dataDirectories();
    for (const QString& directory : directories) {
        if (!QDir().mkpath(directory))
            return {PrepareError::CannotCreateDirectory, directory};
        if (!canCreateFileIn(directory))
            return {PrepareError::NotWritable, directory};
    }
    return {};
}

QString PathManager::toPortable(const QString& absolutePath) const
{
    if (!isInside(m_root, absolutePath))
        return {};
    return normalized(absolutePath).mid(withTrailingSlash(m_root).length());
}

QString PathManager::fromPortable(const QString& portablePath) const
{
    // A colon only appears in drive letters ("C:foo") and alternate data
    // streams; neither is valid in a stored relative path.
    if (portablePath.isEmpty() || portablePath.contains(u':') || QDir::isAbsolutePath(portablePath))
        return {};

    const QString candidate = QDir::cleanPath(withTrailingSlash(m_root) + portablePath);
    return isInside(m_root, candidate) ? candidate : QString();
}

bool PathManager::isInside(const QString& baseDir, const QString& path)
{
    const QString base = normalized(baseDir);
    const QString target = normalized(path);
    if (base.isEmpty() || target.isEmpty())
        return false;

    const QString prefix = withTrailingSlash(base);
    return target.length() > prefix.length() && target.startsWith(prefix, Qt::CaseInsensitive);
}

} // namespace launcher
