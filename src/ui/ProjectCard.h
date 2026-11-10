#pragma once

#include "installer/InstallService.h"
#include "models/Project.h"
#include "ui/Controls.h"

class QLabel;
class QProgressBar;
class QPushButton;

namespace launcher::ui {

/// One project in the library: name, description, category and tags, and what
/// the user can do with it: install, start, uninstall, cancel.
///
/// The card shows the state held by InstallService and InstalledProjects and
/// updates itself when either changes. It decides nothing on its own: a click
/// only calls the service.
class ProjectCard : public Card {
    Q_OBJECT

public:
    ProjectCard(const Project& project, InstallService& service, InstalledProjects& installed,
        QWidget* parent = nullptr);

    QString projectId() const { return m_project.id; }

private:
    void refresh();
    void onPrimaryClicked();
    void onUninstallClicked();

    Project m_project;
    InstallService& m_service;
    InstalledProjects& m_installed;

    QLabel* m_state = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_problem = nullptr;
    QPushButton* m_primary = nullptr;
    QPushButton* m_uninstall = nullptr;
};

} // namespace launcher::ui
