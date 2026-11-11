#include "updater/LauncherUpdater.h"

#include "core/JsonFile.h"
#include "core/Logger.h"
#include "core/PathManager.h"
#include "installer/FileOps.h"
#include "updater/UpdateManifest.h"
#include "updater/UpdateTransaction.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QThread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <vector>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

constexpr int kKeptBackups = 2;

void showMessage(bool enabled, const QString& title, const QString& text)
{
    if (!enabled)
        return;
    MessageBoxW(nullptr, reinterpret_cast<const wchar_t*>(text.utf16()),
        reinterpret_cast<const wchar_t*>(title.utf16()), MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
}

/// Waits until the process has ended. A process that does not exist counts as ended.
/// The process must really be the launcher: a process id can be reused.
bool waitForLauncherToEnd(qint64 processId, const QString& launcherPath, int timeoutMs)
{
    if (processId <= 0)
        return true;
    HANDLE process =
        OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
    if (process == nullptr)
        return true; // gone already (or not ours to wait for)

    std::vector<wchar_t> buffer(4096);
    DWORD size = static_cast<DWORD>(buffer.size());
    if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) {
        const QString image =
            QDir::cleanPath(QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(size)));
        if (image.compare(QDir::cleanPath(launcherPath), Qt::CaseInsensitive) != 0) {
            qCInfo(lcUpdate) << "Process" << processId << "is not the launcher any more; not waiting";
            CloseHandle(process);
            return true;
        }
    }

    const DWORD waited = WaitForSingleObject(process, static_cast<DWORD>(timeoutMs));
    CloseHandle(process);
    if (waited == WAIT_OBJECT_0) {
        QThread::msleep(300); // let Windows and virus scanners let go of the files
        return true;
    }
    return false;
}

bool startProgram(const QString& program, const QString& workingDirectory)
{
    const std::wstring path = QDir::toNativeSeparators(program).toStdWString();
    std::wstring commandLine = L"\"" + path + L"\"";
    const std::wstring directory = QDir::toNativeSeparators(workingDirectory).toStdWString();
    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info {};
    if (!CreateProcessW(path.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_UNICODE_ENVIRONMENT,
            nullptr, directory.c_str(), &startup, &info))
        return false;
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    return true;
}

/// Keeps the newest backups of replaced program files; older ones are deleted.
void pruneBackups(const QString& backupsDir, const QString& keep)
{
    QFileInfoList folders = QDir(backupsDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    int kept = 0;
    for (const QFileInfo& folder : std::as_const(folders)) {
        if (QDir::cleanPath(folder.filePath()) == QDir::cleanPath(keep) || kept < kKeptBackups) {
            ++kept;
            continue;
        }
        FileOps::removeTree(folder.filePath());
    }
}

LauncherUpdater::Result finish(int exitCode, UpdateReport report, const PathManager& paths)
{
    report.finishedAt = QDateTime::currentDateTimeUtc();
    QString error;
    if (!JsonFile::saveAtomic(paths.updateReportFile(), report.toJson(), &error))
        qCWarning(lcUpdate).noquote() << "The report could not be written:" << error;

    LauncherUpdater::Result result;
    result.exitCode = exitCode;
    result.report = report;
    return result;
}

} // namespace

const char* LauncherUpdater::modeFlag()
{
    return "--apply-update";
}

bool LauncherUpdater::isRequested(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], modeFlag()) == 0)
            return true;
    }
    return false;
}

int LauncherUpdater::runProgram(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);

    const QStringList arguments = application.arguments();
    const qsizetype flag = arguments.indexOf(u"--job"_s);
    if (flag < 0 || flag + 1 >= arguments.size())
        return ExitBadJob;

    const JsonFile::LoadResult file = JsonFile::load(arguments.at(flag + 1));
    if (file.status != JsonFile::Status::Ok)
        return ExitBadJob;
    const std::optional<UpdaterJob> job = UpdaterJob::fromJson(file.object);
    if (!job)
        return ExitBadJob;

    return run(*job, !arguments.contains(u"--quiet"_s)).exitCode;
}

LauncherUpdater::Result LauncherUpdater::run(const UpdaterJob& job, bool showMessages)
{
    const QString root = QDir::cleanPath(job.rootDir);
    const PathManager paths(root);

    UpdateReport report;
    report.fromVersion = job.fromVersion;
    report.toVersion = job.toVersion;

    // Only one updater at a time, and the launcher waits for this lock at its next start.
    QDir().mkpath(paths.updaterDir());
    QLockFile lock(paths.updaterLockFile());
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        report.status = UpdateReport::Status::NotApplied;
        report.detail = u"another update is running"_s;
        return finish(ExitNotApplied, report, paths);
    }

    Logger::Options logOptions;
    logOptions.directory = paths.logsDir();
    logOptions.baseName = u"updater"_s;
    logOptions.redactRoot = root;
    Logger::install(logOptions);
    qCInfo(lcUpdate).noquote() << "Updater started: from" << job.fromVersion << "to" << job.toVersion;

    const QString launcherPath = root + u'/' + job.launcherFile;

    // --- the job must make sense before anything else happens --------------------------
    const QString staging = QDir::cleanPath(job.stagingDir);
    if (!QFileInfo(root).isDir() || !PathManager::isInside(root, staging) || !QFileInfo(staging).isDir()) {
        report.status = UpdateReport::Status::NotApplied;
        report.detail = u"the folders of the job are not usable"_s;
        Logger::uninstall();
        return finish(ExitNotApplied, report, paths);
    }

    // --- wait for the launcher -------------------------------------------------------------
    // First, before anything is judged: whatever the verdict, the launcher is started
    // again afterwards, and that must not happen while the old one is still closing.
    if (!waitForLauncherToEnd(job.launcherProcessId, launcherPath, job.waitTimeoutMs)) {
        report.status = UpdateReport::Status::NotApplied;
        report.detail = u"the launcher did not close in time"_s;
        qCWarning(lcUpdate) << "The launcher did not close in time";
        showMessage(showMessages, u"Update nicht möglich"_s,
            u"Das Update konnte nicht beginnen, weil das Programm nicht beendet wurde. Es wurde nichts "
            u"verändert. "
            u"Beende das Programm und starte das Update erneut."_s);
        Logger::uninstall();
        return finish(ExitNotApplied, report, paths);
    }

    // --- the release's manifest ------------------------------------------------------------------
    QFile manifestFile(staging + u'/' + UpdateManifestFormat::kFileName);
    ManifestParseResult manifest;
    if (manifestFile.open(QIODevice::ReadOnly)) {
        const QByteArray text = manifestFile.read(4 * 1024 * 1024);
        manifestFile.close(); // an open file would keep the staging folder from being deleted
        manifest = UpdateManifestFormat::parse(text, {job.product, job.launcherFile});
    } else {
        manifest.error = u"the manifest is missing"_s;
    }
    if (!manifest.ok || manifest.manifest.version != job.toVersion) {
        report.status = UpdateReport::Status::NotApplied;
        report.detail = manifest.ok
            ? u"the manifest is for version %1, not %2"_s.arg(manifest.manifest.version, job.toVersion)
            : manifest.error;
        qCWarning(lcUpdate).noquote() << "Refused:" << report.detail;
        // The launcher has not been touched: start it again, it reports the problem.
        if (job.startLauncherAfter)
            report.launcherStarted = startProgram(launcherPath, root);
        Logger::uninstall();
        return finish(ExitNotApplied, report, paths);
    }

    // --- replace the files --------------------------------------------------------------------
    TransactionPlan plan;
    plan.rootDir = root;
    plan.stagingDir = staging;
    plan.backupDir = paths.updateBackupsDir() + u'/'
        + (job.fromVersion.isEmpty() ? u"unknown"_s : job.fromVersion) + u'-'
        + QDateTime::currentDateTimeUtc().toString(u"yyyyMMddHHmmss"_s);
    plan.journalFile = paths.updateJournalFile();
    plan.fromVersion = job.fromVersion;
    plan.toVersion = job.toVersion;
    plan.manifest = manifest.manifest;

    const TransactionResult applied = UpdateTransaction::apply(plan);
    report.detail = applied.detail;
    report.failedPath = applied.failedPath;
    report.backupDir = QDir(root).relativeFilePath(plan.backupDir);
    qCInfo(lcUpdate).noquote() << "Result:" << static_cast<int>(applied.outcome) << applied.detail
                               << applied.failedPath;

    int exitCode = ExitFailed;
    switch (applied.outcome) {
    case TransactionOutcome::Success:
    case TransactionOutcome::NothingToDo:
        report.status = UpdateReport::Status::Success;
        exitCode = ExitSuccess;
        break;
    case TransactionOutcome::RolledBack:
        report.status = UpdateReport::Status::RolledBack;
        exitCode = ExitRolledBack;
        break;
    case TransactionOutcome::NotApplied:
    case TransactionOutcome::Interrupted:
        report.status = UpdateReport::Status::NotApplied;
        exitCode = ExitNotApplied;
        break;
    case TransactionOutcome::RollbackFailed:
        report.status = UpdateReport::Status::Failed;
        exitCode = ExitFailed;
        break;
    }

    // --- start the launcher again -------------------------------------------------------------
    // Except after a failed rollback: the files are in a mixed state, and the
    // launcher finishes the rollback itself at its next start.
    if (job.startLauncherAfter && report.status != UpdateReport::Status::Failed) {
        report.launcherStarted = startProgram(launcherPath, root);
        if (!report.launcherStarted) {
            qCWarning(lcUpdate) << "The launcher could not be started";
            showMessage(showMessages, u"Programm konnte nicht gestartet werden"_s,
                u"Das Update ist abgeschlossen, aber das Programm konnte nicht gestartet werden. "
                u"Starte es bitte von Hand."_s);
        }
    }
    if (report.status == UpdateReport::Status::Failed) {
        showMessage(showMessages, u"Update fehlgeschlagen"_s,
            u"Das Update ist fehlgeschlagen, und die bisherige Version konnte nicht vollständig "
            u"wiederhergestellt werden. "
            u"Die ersetzten Dateien liegen im Ordner \"%1\". Starte das Programm erneut: Es versucht, den "
            u"alten "
            u"Zustand wiederherzustellen."_s.arg(QDir::toNativeSeparators(plan.backupDir)));
    }

    // The staging folder has served its purpose. The updater's own folder is
    // removed by the launcher at its next start, because this program still runs from it.
    if (report.status == UpdateReport::Status::Success)
        FileOps::removeTree(staging);
    pruneBackups(paths.updateBackupsDir(), plan.backupDir);

    Logger::uninstall();
    return finish(exitCode, report, paths);
}

} // namespace launcher
