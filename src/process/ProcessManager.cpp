#include "process/ProcessManager.h"

#include "core/Logger.h"

#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <tlhelp32.h>

#include <vector>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

HANDLE asHandle(void* handle)
{
    return static_cast<HANDLE>(handle);
}

std::wstring toWide(const QString& text)
{
    return text.toStdWString();
}

/// Program files of all running processes, lowercase with "/" separators and
/// without 8.3 short names, so that they can be compared with a folder path.
QStringList runningImagePaths()
{
    QStringList paths;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return paths;

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    std::vector<wchar_t> buffer(4096);
    std::vector<wchar_t> longBuffer(4096);

    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ProcessID == 0 || entry.th32ProcessID == 4)
            continue; // idle and system
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (process == nullptr)
            continue; // not readable for this user
        DWORD size = static_cast<DWORD>(buffer.size());
        const bool ok = QueryFullProcessImageNameW(process, 0, buffer.data(), &size) != 0;
        CloseHandle(process);
        if (!ok)
            continue;

        QString path;
        const DWORD longSize =
            GetLongPathNameW(buffer.data(), longBuffer.data(), static_cast<DWORD>(longBuffer.size()));
        if (longSize > 0 && longSize < longBuffer.size())
            path = QString::fromWCharArray(longBuffer.data(), static_cast<qsizetype>(longSize));
        else
            path = QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(size));
        paths.append(QDir::cleanPath(path).toLower());
    }
    CloseHandle(snapshot);
    return paths;
}

/// The folder as Windows spells it, lowercase, with a trailing slash; empty if it does not exist.
QString comparableFolder(const QString& folder)
{
    const QString canonical = QFileInfo(folder).canonicalFilePath();
    return canonical.isEmpty() ? QString() : QDir::cleanPath(canonical).toLower() + u'/';
}

bool anyInside(const QStringList& imagePaths, const QString& folderWithSlash)
{
    if (folderWithSlash.isEmpty())
        return false;
    for (const QString& path : imagePaths) {
        if (path.startsWith(folderWithSlash))
            return true;
    }
    return false;
}

} // namespace

ProcessManager::ProcessManager(const LibraryLayout& layout, InstalledProjects& installed, QObject* parent)
    : QObject(parent)
    , m_layout(layout)
    , m_installed(installed)
{
    qRegisterMetaType<launcher::ProcessExit>();
    m_timer.setInterval(kPollIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &ProcessManager::refresh);
    m_timer.start();
}

ProcessManager::~ProcessManager()
{
    // The programs keep running: only the launcher's handles are released.
    QMutexLocker lock(&m_mutex);
    for (const Tracked& tracked : std::as_const(m_tracked)) {
        if (tracked.process)
            CloseHandle(asHandle(tracked.process));
        if (tracked.job)
            CloseHandle(asHandle(tracked.job));
    }
}

void ProcessManager::setPolling(bool enabled)
{
    if (enabled)
        m_timer.start();
    else
        m_timer.stop();
}

bool ProcessManager::jobHasProcesses(const QString& projectId) const
{
    QMutexLocker lock(&m_mutex);
    const auto found = m_tracked.constFind(projectId);
    if (found == m_tracked.cend() || !found->job)
        return false;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info {};
    if (!QueryInformationJobObject(
            asHandle(found->job), JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr))
        return false;
    return info.ActiveProcesses > 0;
}

bool ProcessManager::isRunningNow(const QString& projectId) const
{
    if (jobHasProcesses(projectId))
        return true;
    const std::optional<InstalledProject> record = m_installed.get(projectId);
    if (!record)
        return false;
    return anyInside(runningImagePaths(), comparableFolder(m_layout.currentDir(record->installDirectory)));
}

QSet<QString> ProcessManager::lookUp() const
{
    QSet<QString> running;
    const QList<InstalledProject> projects = m_installed.all();

    // One snapshot of the process list serves all projects.
    const QStringList images = projects.isEmpty() ? QStringList() : runningImagePaths();
    for (const InstalledProject& project : projects) {
        if (jobHasProcesses(project.projectId)
            || anyInside(images, comparableFolder(m_layout.currentDir(project.installDirectory))))
            running.insert(project.projectId);
    }
    return running;
}

void ProcessManager::refresh()
{
    // Looked up first, then the ends are collected: a program that ended before
    // the look-up is reported in this very call, and always before the project
    // is reported as no longer running.
    const QSet<QString> now = lookUp();
    reportExits();
    const QSet<QString> before = m_running;
    m_running = now;

    // Sorted, so that the order of the signals does not depend on hashing.
    QStringList changed = (now - before).values();
    changed.sort();
    for (const QString& id : std::as_const(changed))
        emit runningChanged(id, true);
    changed = (before - now).values();
    changed.sort();
    for (const QString& id : std::as_const(changed))
        emit runningChanged(id, false);

    // A job with nothing left in it is not needed any more.
    QMutexLocker lock(&m_mutex);
    for (auto it = m_tracked.begin(); it != m_tracked.end();) {
        bool empty = false;
        if (!it->process && it->job) {
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info {};
            empty = QueryInformationJobObject(
                        asHandle(it->job), JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr)
                && info.ActiveProcesses == 0;
        }
        if (empty) {
            CloseHandle(asHandle(it->job));
            it = m_tracked.erase(it);
        } else {
            ++it;
        }
    }
}

void ProcessManager::reportExits()
{
    QList<std::pair<QString, ProcessExit>> ended;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_tracked.begin(); it != m_tracked.end(); ++it) {
            if (!it->process || WaitForSingleObject(asHandle(it->process), 0) != WAIT_OBJECT_0)
                continue;
            DWORD code = 0;
            GetExitCodeProcess(asHandle(it->process), &code);
            CloseHandle(asHandle(it->process));
            it->process = nullptr;

            ProcessExit exit;
            exit.exitCode = static_cast<int>(code);
            exit.runSeconds = it->startedAt.secsTo(QDateTime::currentDateTimeUtc());
            ended.append({it.key(), exit});
        }
    }

    for (const auto& [id, exit] : std::as_const(ended)) {
        m_exits.insert(id, exit);
        qCInfo(lcProcess).noquote() << "Program of" << id << "ended with code" << exit.exitCode << "after"
                                    << exit.runSeconds << "s" << (exit.unexpected() ? "(unexpected)" : "");
        emit exited(id, exit);
    }
}

void ProcessManager::clearLastExit(const QString& projectId)
{
    m_exits.remove(projectId);
}

StartResult ProcessManager::start(const QString& projectId)
{
    const auto failure = [](StartError error, const QString& detail) {
        StartResult result;
        result.error = error;
        result.detail = detail;
        return result;
    };

    const StartTarget target = resolveStartTarget(m_layout, m_installed, projectId);
    if (!target.ok())
        return failure(target.error, target.detail);

    if (isRunningNow(projectId))
        return failure(StartError::AlreadyRunning, u"the project is running"_s);

    std::wstring commandLine = L"\"" + toWide(QDir::toNativeSeparators(target.program)) + L"\"";
    const std::wstring program = toWide(QDir::toNativeSeparators(target.program));
    const std::wstring directory = toWide(QDir::toNativeSeparators(target.workingDirectory));

    STARTUPINFOW startup {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info {};
    // Suspended, so that the program is in its job before it can start anything.
    if (!CreateProcessW(program.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, nullptr, directory.c_str(), &startup, &info))
        return failure(StartError::StartFailed, u"CreateProcess failed with error %1"_s.arg(GetLastError()));

    // The job holds the program and everything it starts. Breakaway is allowed
    // so that a program that asks for it (some updaters do) is not refused;
    // such a descendant is then found by its program file instead.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_BREAKAWAY_OK;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))
            || !AssignProcessToJobObject(job, info.hProcess)) {
            qCWarning(lcProcess) << "Could not put the program of" << projectId << "into a job, error"
                                 << GetLastError();
            CloseHandle(job);
            job = nullptr;
        }
    }

    ResumeThread(info.hThread);
    CloseHandle(info.hThread);

    {
        QMutexLocker lock(&m_mutex);
        Tracked& tracked = m_tracked[projectId];
        if (tracked.process)
            CloseHandle(asHandle(tracked.process));
        if (tracked.job)
            CloseHandle(asHandle(tracked.job));
        tracked.process = info.hProcess;
        tracked.job = job;
        tracked.startedAt = QDateTime::currentDateTimeUtc();
    }
    m_exits.remove(projectId);

    qCInfo(lcProcess).noquote() << "Started" << projectId << "pid" << info.dwProcessId;
    m_installed.recordStart(projectId, QDateTime::currentDateTimeUtc());

    const bool wasRunning = m_running.contains(projectId);
    m_running.insert(projectId);
    emit started(projectId);
    if (!wasRunning)
        emit runningChanged(projectId, true);

    StartResult result;
    result.processId = info.dwProcessId;
    return result;
}

} // namespace launcher
