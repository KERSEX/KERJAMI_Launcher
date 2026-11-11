#pragma once

#include "models/Project.h"

#include <functional>

class QWidget;

namespace launcher {
struct UpdateInfo;
struct LauncherUpdateInfo;
}

namespace launcher::ui::Dialogs {

/// Asks whether a project may be removed. Nothing is deleted without a yes.
/// The text says what is deleted and that it cannot be undone.
bool confirmUninstall(QWidget* parent, const Project& project);

/// Asks whether a project may be updated to the newest version and shows its
/// changelog (the release notes). Nothing is downloaded without a yes.
bool confirmUpdate(QWidget* parent, const Project& project, const UpdateInfo& update);

/// Asks whether the launcher may update itself and shows the changelog. The
/// text says that the launcher will close and start again, that running
/// projects stay open, and that settings, projects and downloads stay as they are.
bool confirmLauncherUpdate(QWidget* parent, const LauncherUpdateInfo& info);

/// What the storage cleanup of the settings deletes.
enum class StorageCleanup {
    DownloadsAndTemp, ///< downloads/ and temp/
    Backups,          ///< the saved earlier versions of all projects
};

/// Asks whether a cleanup may delete what it says; @p size is the amount in readable form.
/// Nothing is deleted without a yes.
bool confirmStorageCleanup(QWidget* parent, StorageCleanup what, const QString& size);

/// Replaces the dialog with a function that answers at once. Only tests use
/// this, because a modal dialog cannot be answered from inside a test. An empty
/// function restores the dialog.
void setUninstallConfirmationForTests(std::function<bool(const Project&)> answer);
void setUpdateConfirmationForTests(std::function<bool(const Project&, const UpdateInfo&)> answer);
void setLauncherUpdateConfirmationForTests(std::function<bool(const LauncherUpdateInfo&)> answer);
void setStorageCleanupConfirmationForTests(std::function<bool(StorageCleanup)> answer);

} // namespace launcher::ui::Dialogs
