#include "ui/ProjectCard.h"

#include "ui/FlowLayout.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace launcher::ui {

namespace {

QLabel* createChip(const QString& text, const char* role, QWidget* parent)
{
    QLabel* chip = createLabel(text, role, parent);
    chip->setAlignment(Qt::AlignCenter);
    return chip;
}

} // namespace

ProjectCard::ProjectCard(const Project& project, QWidget* parent)
    : Card(parent)
    , m_projectId(project.id)
{
    setObjectName("ProjectCard");
    body()->setSpacing(8);

    auto* header = new QHBoxLayout;
    header->setSpacing(10);
    QLabel* name = createLabel(project.name, "title", this);
    name->setObjectName("ProjectName");
    header->addWidget(name);
    if (project.featured) {
        QLabel* badge = createChip(tr("Empfohlen"), "badge", this);
        badge->setObjectName("ProjectFeatured");
        header->addWidget(badge);
    }
    header->addStretch(1);
    QLabel* category = createLabel(project.category, "secondary", this);
    category->setObjectName("ProjectCategory");
    header->addWidget(category);
    body()->addLayout(header);

    QLabel* description = createLabel(project.description, "secondary", this);
    description->setWordWrap(true);
    body()->addWidget(description);

    if (!project.tags.isEmpty()) {
        auto* tags = new FlowLayout;
        for (const QString& tag : project.tags)
            tags->addWidget(createChip(tag, "chip", this));
        body()->addLayout(tags);
    }
}

} // namespace launcher::ui
