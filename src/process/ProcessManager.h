#pragma once

#include "installer/InstalledProjects.h"
#include "installer/LibraryLayout.h"
#include "installer/ProjectLauncher.h"

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <optional>

namespace launcher {

/// How a project that the launcher started came to an end.
struct ProcessExit {
    int exitCode = 0;
    qint64 runSeconds = 0;

    /// Windows reports a crash (access violation, stack overflow, ...) as an exit code with the high bits
    /// set.
    bool crashed() const { return static_cast<quint32>(exitCode) >= 0xC0000000u; }

    /// Ended with an error code a few seconds after the start, or crashed.
    /// A long run that ends with a non-zero code is common and not unexpected.
    bool unexpected() const { return crashed() || (exitCode != 0 && runSeconds < 10); }
};

/// Starts installed projects and knows which of them are running.
///
/// A project counts as running if
///  - a program the launcher started for it is alive, together with everything
///    that program started (Windows tracks the whole family in a job object), or
///  - any process on the computer has its program file inside the project's
///    current/ folder. This also finds a project that was started from Explorer,
///    or by an earlier launcher run, and it is why closing the launcher window
///    never makes a project look closed.
///
/// Limits, on purpose: a helper program that lives outside current/ and was not
/// started by a program the launcher started is not seen, and neither is a
/// process whose program file Windows does not let this launcher read (rare;
/// processes of other users). The installer's file lock check is the last
/// safety net: a folder with a program in use cannot be replaced.
class ProcessManager : public QObject {
    Q_OBJECT

public:
    /// How often running projects are looked up.
    static constexpr int kPollIntervalMs = 2000;

    ProcessManager(const LibraryLayout& layout, InstalledProjects& installed, QObject* parent = nullptr);
    ~ProcessManager() override;

    /// Switches the periodic look-up on or off. On by default. Tests call refresh() themselves.
    void setPolling(bool enabled);

    /// Starts the project's program, unless it is running already
    /// (StartError::AlreadyRunning). The program keeps running if the launcher is closed.
    StartResult start(const QString& projectId);

    /// As of the last look-up. Only for the user interface; decisions use isRunningNow().
    bool isRunning(const QString& projectId) const { return m_running.contains(projectId); }
    QStringList runningProjects() const { return m_running.values(); }

    /// Looks it up right now. May be called from any thread; the installer
    /// asks this from its worker thread before it touches a project's files.
    bool isRunningNow(const QString& projectId) const;

    /// Looks up all projects now and reports changes. Called by the timer.
    void refresh();

    /// How the last program started by this launcher ended, if it did.
    std::optional<ProcessExit> lastExit(const QString& projectId) const
    {
        const auto found = m_exits.constFind(projectId);
        return found == m_exits.cend() ? std::nullopt : std::optional<ProcessExit>(*found);
    }
    void clearLastExit(const QString& projectId);

signals:
    /// The launcher started a project's program.
    void started(const QString& projectId);

    /// A project began or stopped running, whoever started it.
    void runningChanged(const QString& projectId, bool running);

    /// The program the launcher started for a project has ended. Other programs of the project may still run.
    void exited(const QString& projectId, const launcher::ProcessExit& exit);

private:
    /// What Windows needs to keep track of one started program.
    struct Tracked {
        void* process = nullptr; ///< HANDLE of the started program; null once its end was reported.
        void* job = nullptr;     ///< HANDLE of the job object that holds the program and its descendants.
        QDateTime startedAt;
    };

    QSet<QString> lookUp() const;
    bool jobHasProcesses(const QString& projectId) const;
    void reportExits();

    LibraryLayout m_layout;
    InstalledProjects& m_installed;
    QTimer m_timer;

    mutable QMutex m_mutex; ///< Guards m_tracked and the handles in it.
    QHash<QString, Tracked> m_tracked;

    QSet<QString> m_running; ///< Main thread only.
    QHash<QString, ProcessExit> m_exits;
};

} // namespace launcher

Q_DECLARE_METATYPE(launcher::ProcessExit)
