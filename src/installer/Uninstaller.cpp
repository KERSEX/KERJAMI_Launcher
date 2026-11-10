#include "installer/Uninstaller.h"

#include "core/Logger.h"
#include "core/PathManager.h"
#include "github/RegistryParser.h"
#include "installer/FileOps.h"

#include <QDir>
#include <QFileInfo>
#include <QThread>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

UninstallResult failure(UninstallError error, const QString& detail)
{
    UninstallResult result;
    result.error = error;
    result.detail = detail;
    return result;
}

} // namespace

Uninstaller::Uninstaller(const LibraryLayout& layout, InstalledProjects& installed, QObject* parent)
    : QObject(parent)
    , m_layout(layout)
    , m_installed(installed)
{
}

Uninstaller::~Uninstaller()
{
    // A deletion that is still running finishes; the rest is left to cleanupTemp().
    for (QThread* thread : std::as_const(m_cleanups)) {
        thread->wait();
        delete thread;
    }
}

UninstallResult Uninstaller::uninstall(const QString& projectId)
{
    const std::optional<InstalledProject> record = m_installed.get(projectId);
    if (!record)
        return failure(UninstallError::NotInstalled, u"no record for \"%1\""_s.arg(projectId));

    // The record was checked when it was loaded; the folder it names decides
    // what is deleted, so it is checked once more right before use.
    if (!RegistryParser::isValidProjectId(projectId)
        || !RegistryParser::isValidInstallDirectory(record->installDirectory))
        return failure(UninstallError::ProtectedPath, u"the record holds an invalid folder name"_s);

    const QString projectDir = m_layout.projectDir(record->installDirectory);
    if (!PathManager::isInside(m_layout.libraryDir(), projectDir))
        return failure(UninstallError::ProtectedPath, u"\"%1\" is not inside the library"_s.arg(projectDir));
    if (FileOps::isLink(projectDir))
        return failure(UninstallError::ProtectedPath, u"\"%1\" is a link"_s.arg(projectDir));

    if (m_isRunning && m_isRunning(projectId))
        return failure(UninstallError::ProjectRunning, u"the project is running"_s);

    // Nothing to delete if the folder was removed by hand: only the record goes.
    if (!QFileInfo::exists(projectDir)) {
        QString error;
        if (!m_installed.remove(projectId, &error))
            return failure(UninstallError::RegistrationFailed, error);
        qCInfo(lcInstall).noquote() << "Removed the record of" << projectId << "(folder was already gone)";
        emit uninstalled(projectId);
        return {};
    }

    const QString trash = m_layout.trashDir(projectId);
    QString error;
    if (!FileOps::removeTree(trash, &error))
        return failure(
            UninstallError::ProjectRunning, u"an earlier removal cannot be cleaned up: %1"_s.arg(error));
    if (!FileOps::renameWithRetry(projectDir, trash, &error))
        return failure(UninstallError::ProjectRunning, error);

    if (!m_installed.remove(projectId, &error)) {
        QString undoError;
        if (!FileOps::renameWithRetry(trash, projectDir, &undoError))
            qCCritical(lcInstall).noquote()
                << "Could not restore" << projectDir << "after a failed removal:" << undoError;
        return failure(UninstallError::RegistrationFailed, error);
    }

    qCInfo(lcInstall).noquote() << "Uninstalled" << projectId;
    emit uninstalled(projectId);

    QThread* thread = QThread::create([this, projectId, trash] {
        QString failedPath;
        const bool complete = FileOps::removeTree(trash, &failedPath);
        if (!complete)
            qCWarning(lcInstall).noquote()
                << "Removed project not fully deleted, left for the next start:" << failedPath;
        QMetaObject::invokeMethod(
            this, [this, projectId, complete] { emit cleanupFinished(projectId, complete); },
            Qt::QueuedConnection);
    });
    m_cleanups.append(thread);
    connect(thread, &QThread::finished, this, [this, thread] {
        m_cleanups.removeOne(thread);
        thread->deleteLater();
    });
    thread->start();
    return {};
}

} // namespace launcher
