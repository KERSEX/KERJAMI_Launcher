#pragma once

#include "models/Project.h"
#include "ui/AppContext.h"
#include "ui/Controls.h"

class QLabel;

namespace launcher::ui {

/// One project in the library: name, description, category and tags, the
/// "Details" button, and the controls to install, start, update and remove it
/// (ProjectControls).
///
/// The card shows what the managers know and updates itself when they change.
/// It decides nothing on its own.
class ProjectCard : public Card {
    Q_OBJECT

public:
    ProjectCard(const Project& project, const AppContext& context, QWidget* parent = nullptr);

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
