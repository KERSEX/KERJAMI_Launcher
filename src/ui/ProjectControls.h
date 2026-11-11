#pragma once

#include "models/Project.h"
#include "ui/AppContext.h"
#include "ui/Controls.h"

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QProgressBar;
class QPushButton;

namespace launcher::ui {

/// What the user can do with one project, and how it is going: install, start,
/// update, uninstall, cancel, the choice between manual and automatic updates,
/// the status line, the progress bar and the last problem.
///
/// Shared by the store, the library and the detail page; the mode decides how much of
/// it a place shows. It shows what the managers know and updates itself when they
/// change; it decides nothing on its own, a click only calls the service or the
/// update manager.
class ProjectControls : public QWidget {
    Q_OBJECT

public:
    enum class Mode {
        Full,    ///< Everything: the detail page, and a project that is not in the list any more.
        Library, ///< An installed project in the library: start and update, nothing that removes or
                 ///< reconfigures.
        Store, ///< A project in the store: install, cancel and update. Nothing about running or removing it.
    };

    ProjectControls(const Project& project, const AppContext& context, Mode mode, QWidget* parent = nullptr);

    /// Puts a button to the left of the project's own buttons.
    void addLeadingButton(QPushButton* button);

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
    Mode m_mode;

    QHBoxLayout* m_actions = nullptr;
    QLabel* m_state = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_problem = nullptr;
    ComboBox* m_updateMode = nullptr;
    QPushButton* m_primary = nullptr;
    QPushButton* m_update = nullptr;
    QPushButton* m_uninstall = nullptr;
};

} // namespace launcher::ui
