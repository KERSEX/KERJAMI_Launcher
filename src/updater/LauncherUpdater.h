#pragma once

#include "updater/UpdaterProtocol.h"

namespace launcher {

/// The update run: wait for the launcher to end, replace its program files from
/// the staged release, start the launcher again.
///
/// The launcher program does this itself, in a second mode: `KERJAMI_Launcher.exe
/// --apply-update --job <job.json> [--quiet]`. A running program cannot replace its
/// own file, so the launcher starts a copy of itself from temp/ in this mode and
/// quits; the copy then replaces the file in the program folder. There is no
/// separate updater program (docs/03-entscheidungen.md, D-40). The run touches
/// only what the manifest of the release lists, rolls back on any failure, and
/// leaves a report for the launcher to show.
class LauncherUpdater {
public:
    /// Exit codes of the program in this mode.
    enum ExitCode {
        ExitSuccess = 0,
        ExitRolledBack = 1, ///< The update failed; the old version is back.
        ExitNotApplied = 2, ///< Nothing was changed.
        ExitFailed = 3,     ///< The update failed and the old version could not be fully restored.
        ExitBadJob = 4,     ///< The job file is missing or unusable.
    };

    struct Result {
        int exitCode = ExitBadJob;
        UpdateReport report;
    };

    /// The command line argument that selects this mode.
    static const char* modeFlag();

    /// True if the command line asks for the update run. Looks at the raw
    /// arguments, so main() can decide before any application object exists.
    static bool isRequested(int argc, char* argv[]);

    /// The whole program in this mode: reads the job file named by --job, runs it
    /// and returns the exit code. Creates its own QCoreApplication: no window, no
    /// network.
    static int runProgram(int argc, char* argv[]);

    /// Runs the job. Shows a message box only where nobody else could tell the
    /// user (see showMessages); everything else goes into the report and the log.
    static Result run(const UpdaterJob& job, bool showMessages);
};

} // namespace launcher
