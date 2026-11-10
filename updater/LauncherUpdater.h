#pragma once

#include "updater/UpdaterProtocol.h"

namespace launcher {

/// The work of LauncherUpdater.exe: wait for the launcher to end, replace its
/// program files from the staged release, start the launcher again.
///
/// The updater is a separate program because a running program cannot replace
/// its own file. It runs from a copy in temp/, so even its own file in the
/// launcher's folder can be replaced. It touches only what the manifest of the
/// release lists, rolls back on any failure, and leaves a report for the
/// launcher to show.
class LauncherUpdater {
public:
    /// Exit codes of the program.
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

    /// Runs the job. Shows a message box only where nobody else could tell the
    /// user (see showMessages); everything else goes into the report and the log.
    static Result run(const UpdaterJob& job, bool showMessages);
};

} // namespace launcher
