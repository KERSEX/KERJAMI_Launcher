#pragma once

#include "ui/Page.h"

namespace launcher {
class LauncherUpdateManager;
class PathManager;
class SettingsManager;
}

namespace launcher::ui {

/// Preferences of the launcher. Every control writes through to the
/// SettingsManager as soon as it changes; there is no "Apply" button.
class SettingsPage : public Page {
    Q_OBJECT

public:
    SettingsPage(const PathManager& paths, SettingsManager& settings, LauncherUpdateManager& launcherUpdates,
        QWidget* parent = nullptr);

private:
    QWidget* createStorageCard(const PathManager& paths);
    QWidget* createUpdatesCard();
    QWidget* createStartupCard();
    QWidget* createAboutCard(const PathManager& paths, LauncherUpdateManager& launcherUpdates);

    /// Title and description on the left, an optional control on the right.
    QWidget* createRow(const QString& title, const QString& description, QWidget* control);

    SettingsManager& m_settings;
};

} // namespace launcher::ui
