#pragma once

// Helpers for tests that need a real running program: SleepyApp, built next to
// the tests, is copied into a project's current/ folder and registered as the
// installed project.

#include "installer/InstalledProjects.h"
#include "installer/LibraryLayout.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace sleepy {

inline QString appPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/SleepyApp.exe");
}

inline bool writeControlFile(const QString& folder, const QString& name, const QByteArray& content = "1")
{
    QDir().mkpath(folder);
    QFile file(folder + QLatin1Char('/') + name);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

/// Puts SleepyApp into library/<directory>/current/<executable> and records the project as installed.
inline bool install(const launcher::LibraryLayout& layout, launcher::InstalledProjects& installed,
    const QString& projectId, const QString& directory, const QString& version,
    const QString& executable = QStringLiteral("Tool.exe"))
{
    const QString current = layout.currentDir(directory);
    QDir().mkpath(current);
    const QString target = current + QLatin1Char('/') + executable;
    QFile::remove(target);
    if (!QFile::copy(appPath(), target))
        return false;

    launcher::InstalledProject record;
    record.projectId = projectId;
    record.version = version;
    record.tag = QStringLiteral("v") + version;
    record.installDirectory = directory;
    record.executable = executable;
    record.installedAt = QDateTime::currentDateTimeUtc();
    return installed.upsert(record);
}

/// Tells every SleepyApp that reads this folder's control files to end.
inline void stop(const QString& currentDir)
{
    writeControlFile(currentDir, QStringLiteral("stop"));
}

} // namespace sleepy
