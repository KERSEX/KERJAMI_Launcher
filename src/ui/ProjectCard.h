#pragma once

#include "installer/InstallService.h"
#include "models/Project.h"
#include "ui/AppContext.h"
#include "ui/Controls.h"

class QLabel;
class QProgressBar;
class QPushButton;

namespace launcher::ui {

/// One project in the library: name, description, category and tags, and what
/// the user can do with it: install, start, update, uninstall, cancel, and
/// choose how it is updated.
///
/// The card shows what the managers know and updates itself when they change.
/// It decides nothing on its own: a click only calls the service or the update
/// manager.
class ProjectCard : public Card {
    Q_OBJECT

public:
    ProjectCard(const Project& project, const AppContext& context, QWidget* parent = nullptr);

    QString projectId() const { return m_project.id; }

private:
    void refresh();
    void onPrimaryClicked();
    void onUpdateClicked();
    void onUninstallClicked();
    void onUpdateModeChosen(int index);

    /// The sentence for the red line: the last failure, or an unexpected end of the program.
    QString problemText() const;

    Project m_project;
    AppContext m_context;

    QLabel* m_updateBadge = nullptr;
    QLabel* m_state = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_problem = nullptr;
    ComboBox* m_updateMode = nullptr;
    QPushButton* m_primary = nullptr;
    QPushButton* m_update = nullptr;
    QPushButton* m_uninstall = nullptr;
};

} // namespace launcher::ui
