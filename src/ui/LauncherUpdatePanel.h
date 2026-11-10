#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;

namespace launcher {
class LauncherUpdateManager;
}

namespace launcher::ui {

/// The launcher's own update on one screen element: what is known, what is
/// going on, and the buttons that fit the state (look for updates, update,
/// restart now, cancel). It shows the state of the LauncherUpdateManager and
/// calls it; it decides nothing. The same element sits on the dashboard and on
/// the settings page.
class LauncherUpdatePanel : public QWidget {
    Q_OBJECT

public:
    LauncherUpdatePanel(LauncherUpdateManager& manager, QWidget* parent = nullptr);

private:
    void refresh();
    void onUpdateClicked();

    LauncherUpdateManager& m_manager;
    QLabel* m_status = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_problem = nullptr;
    QPushButton* m_check = nullptr;
    QPushButton* m_update = nullptr;
    QPushButton* m_restart = nullptr;
    QPushButton* m_cancel = nullptr;
};

} // namespace launcher::ui
