#include "installer/ProjectLauncher.h"

#include "core/Logger.h"
#include "core/PathManager.h"
#include "installer/FileOps.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

StartResult failure(StartError error, const QString& detail)
{
    StartResult result;
    result.error = error;
    result.detail = detail;
    return result;
}

} // namespace

StartResult startProject(const LibraryLayout& layout, InstalledProjects& installed, const QString& projectId)
{
    const std::optional<InstalledProject> record = installed.get(projectId);
    if (!record)
        return failure(StartError::NotInstalled, u"no record for \"%1\""_s.arg(projectId));

    const QString current = layout.currentDir(record->installDirectory);
    const QString program = QDir::cleanPath(layout.executablePath(*record));
    if (!PathManager::isInside(current, program))
        return failure(StartError::ProtectedPath, u"\"%1\" is not inside \"%2\""_s.arg(program, current));
    if (FileOps::isLink(current))
        return failure(StartError::ProtectedPath, u"\"%1\" is a link"_s.arg(current));

    const QFileInfo file(program);
    if (!file.isFile())
        return failure(StartError::FilesMissing, u"\"%1\" does not exist"_s.arg(program));
    if (file.isSymLink() || file.isJunction())
        return failure(StartError::ProtectedPath, u"\"%1\" is a link"_s.arg(program));

    qint64 pid = 0;
    if (!QProcess::startDetached(program, {}, current, &pid))
        return failure(StartError::StartFailed, u"could not start \"%1\""_s.arg(program));

    qCInfo(lcInstall).noquote() << "Started" << projectId << "pid" << pid;
    installed.recordStart(projectId, QDateTime::currentDateTimeUtc());

    StartResult result;
    result.processId = pid;
    return result;
}

} // namespace launcher
