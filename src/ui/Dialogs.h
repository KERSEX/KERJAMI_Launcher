#pragma once

#include "models/Project.h"

#include <functional>

class QWidget;

namespace launcher {
struct UpdateInfo;
}

namespace launcher::ui::Dialogs {

/// Asks whether a project may be removed. Nothing is deleted without a yes.
/// The text says what is deleted and that it cannot be undone.
bool confirmUninstall(QWidget* parent, const Project& project);

/// Asks whether a project may be updated to the newest version and shows its
/// changelog (the release notes). Nothing is downloaded without a yes.
bool confirmUpdate(QWidget* parent, const Project& project, const UpdateInfo& update);

/// Replaces the dialog with a function that answers at once. Only tests use
/// this, because a modal dialog cannot be answered from inside a test. An empty
/// function restores the dialog.
void setUninstallConfirmationForTests(std::function<bool(const Project&)> answer);
void setUpdateConfirmationForTests(std::function<bool(const Project&, const UpdateInfo&)> answer);

} // namespace launcher::ui::Dialogs
