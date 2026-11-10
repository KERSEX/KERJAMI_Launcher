#include "updater/UpdateStartup.h"

#include "core/JsonFile.h"
#include "core/Logger.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QThread>

using namespace Qt::StringLiterals;

namespace launcher::UpdateStartup {

namespace {

/// True if an updater holds its lock after the waiting time.
bool updaterStillWorking(const PathManager& paths, int waitMs)
{
    // No folder, no updater has ever run here.
    if (!QFileInfo(paths.updaterDir()).isDir() || !QFileInfo::exists(paths.updaterLockFile()))
        return false;

    QElapsedTimer timer;
    timer.start();
    for (;;) {
        QLockFile lock(paths.updaterLockFile());
        lock.setStaleLockTime(0);
        if (lock.tryLock(0))
            return false; // free (a lock of a process that is gone counts as free)
        if (timer.elapsed() >= waitMs)
            return true;
        QThread::msleep(200);
    }
}

} // namespace

StartupUpdateOutcome run(const PathManager& paths, int waitForUpdaterMs)
{
    StartupUpdateOutcome outcome;

    if (updaterStillWorking(paths, waitForUpdaterMs)) {
        qCWarning(lcUpdate) << "An updater is still working; the launcher does not start";
        outcome.updaterStillRunning = true;
        return outcome;
    }

    // An update that was killed in the middle leaves a journal. The files
    // themselves tell whether it was finished or has to be rolled back.
    const TransactionResult recovered = UpdateTransaction::recover(paths.root(), paths.updateJournalFile());
    if (recovered.outcome != TransactionOutcome::NothingToDo) {
        qCWarning(lcUpdate).noquote()
            << "An interrupted update was found:" << static_cast<int>(recovered.outcome) << recovered.detail;
        outcome.recovery = recovered;
    }

    const JsonFile::LoadResult file = JsonFile::load(paths.updateReportFile());
    if (file.status == JsonFile::Status::Ok) {
        outcome.report = UpdateReport::fromJson(file.object);
        QFile::remove(paths.updateReportFile()); // shown once
    } else if (file.status != JsonFile::Status::NotFound) {
        QFile::remove(
            paths.updateReportFile()); // unreadable: of no use, and it must not come back at every start
    }
    return outcome;
}

} // namespace launcher::UpdateStartup
