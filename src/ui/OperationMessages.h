#pragma once

#include "installer/InstallService.h"
#include "process/ProcessManager.h"
#include "updater/LauncherUpdateManager.h"
#include "updater/UpdateTransaction.h"
#include "updater/UpdaterProtocol.h"

#include <QString>

#include <optional>

namespace launcher::ui {

/// The sentence shown to the user when installing, removing or starting a
/// project failed. It says what happened and, where the user can act, what to do.
QString failureText(const OperationFailure& failure);

/// A program the launcher started ended with an error or crashed.
QString exitText(const ProcessExit& exit);

/// What the launcher is doing right now, for example "Download läuft: 12,3 MB von 45,6 MB".
QString stageText(const ProjectStatus& status);

/// The status line of the launcher's own update: what is known and what is going on.
QString launcherUpdateStatusText(const LauncherUpdateInfo& info);

/// The sentence for a failed step of the launcher's own update. Empty for the
/// "waiting for installations" note, which the status line carries.
QString launcherUpdateFailureText(const LauncherUpdateFailure& failure);

/// A message to show when the launcher starts: how an update ended, or what was done about an interrupted
/// one.
struct StartupMessage {
    enum class Level {
        Information,
        Warning,
        Critical,
    };

    Level level = Level::Information;
    QString title;
    QString text;
};

/// The message for the report of the last update. Empty if there is nothing to say.
std::optional<StartupMessage> updateReportMessage(const UpdateReport& report);

/// The message for what the start did about an interrupted update.
std::optional<StartupMessage> recoveryMessage(const TransactionResult& result);

} // namespace launcher::ui
