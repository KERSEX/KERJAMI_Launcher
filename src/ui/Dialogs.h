#pragma once

#include "models/Project.h"

#include <functional>

class QWidget;

namespace launcher::ui::Dialogs {

/// Asks whether a project may be removed. Nothing is deleted without a yes.
/// The text says what is deleted and that it cannot be undone.
bool confirmUninstall(QWidget* parent, const Project& project);

/// Replaces the dialog with a function that answers at once. Only tests use
/// this, because a modal dialog cannot be answered from inside a test. An empty
/// function restores the dialog.
void setUninstallConfirmationForTests(std::function<bool(const Project&)> answer);

} // namespace launcher::ui::Dialogs
