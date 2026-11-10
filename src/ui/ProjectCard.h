#pragma once

#include "models/Project.h"
#include "ui/Controls.h"

namespace launcher::ui {

/// One project in the library: name, description, category and tags.
class ProjectCard : public Card {
    Q_OBJECT

public:
    explicit ProjectCard(const Project& project, QWidget* parent = nullptr);

    QString projectId() const { return m_projectId; }

private:
    QString m_projectId;
};

} // namespace launcher::ui
