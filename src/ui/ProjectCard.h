#pragma once

#include "models/Project.h"
#include "ui/AppContext.h"
#include "ui/Controls.h"
#include "ui/ProjectControls.h"

class QLabel;

namespace launcher::ui {

/// One installed project in the library: its name, a note if an update is known, the
/// version and state, and the buttons to start and update it (ProjectControls).
///
/// The card is kept short on purpose: what a project is, what it does and how it is
/// removed belong to its detail page. A project that is no longer in the list has no
/// detail page, so its card carries every control (Mode::Full) and no "Details" button.
///
/// The card shows what the managers know and updates itself when they change.
/// It decides nothing on its own.
class ProjectCard : public Card {
    Q_OBJECT

public:
    /// @p mode is Library for a project of the list and Full for one that has left it.
    ProjectCard(const Project& project, const AppContext& context, ProjectControls::Mode mode,
        QWidget* parent = nullptr);

    QString projectId() const { return m_project.id; }

signals:
    /// The user wants the detail page of this project.
    void detailsRequested(const QString& projectId);

private:
    void refresh();

    Project m_project;
    AppContext m_context;

    QLabel* m_updateBadge = nullptr;
};

} // namespace launcher::ui
