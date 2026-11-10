#pragma once

#include "core/PathManager.h"
#include "updater/UpdateTransaction.h"
#include "updater/UpdaterProtocol.h"

#include <optional>

namespace launcher {

/// What the start of the launcher finds out about updates of itself.
struct StartupUpdateOutcome {
    /// An updater was still working after the waiting time. The launcher must
    /// not start on files that are being replaced.
    bool updaterStillRunning = false;

    /// An earlier update was interrupted (the updater was killed, the power failed);
    /// this is what was done about it.
    std::optional<TransactionResult> recovery;

    /// How the last update ended, to be shown to the user once.
    std::optional<UpdateReport> report;
};

/// The launcher's first steps with respect to its own update.
///
/// In order: wait for an updater that is still working (it holds a lock; the
/// launcher the updater started can be quicker than the updater's last step),
/// finish or roll back an update that was interrupted, and read the report of
/// the last update. The report is deleted once read: it is shown once.
namespace UpdateStartup {

StartupUpdateOutcome run(const PathManager& paths, int waitForUpdaterMs = 60000);

} // namespace UpdateStartup

} // namespace launcher
