#pragma once

#include "installer/InstallService.h"
#include "process/ProcessManager.h"

#include <QString>

namespace launcher::ui {

/// The sentence shown to the user when installing, removing or starting a
/// project failed. It says what happened and, where the user can act, what to do.
QString failureText(const OperationFailure& failure);

/// A program the launcher started ended with an error or crashed.
QString exitText(const ProcessExit& exit);

/// What the launcher is doing right now, for example "Download läuft: 12,3 MB von 45,6 MB".
QString stageText(const ProjectStatus& status);

} // namespace launcher::ui
