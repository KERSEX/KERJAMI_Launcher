#include "ui/ProjectCard.h"

#include "installer/InstallService.h"
#include "installer/InstalledProjects.h"
#include "updater/ProjectUpdateManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

using namespace Qt::StringLiterals;

namespace launcher::ui {

ProjectCard::ProjectCard(
    const Project& project, const AppContext& context, ProjectControls::Mode mode, QWidget* parent)
    : Card(parent)
    , m_project(project)
    , m_context(context)
{
    setObjectName("ProjectCard");
    body()->setSpacing(8);

    auto* header = new QHBoxLayout;
    header->setSpacing(10);
    QLabel* name = createLabel(project.name, "title", this);
    name->setObjectName("ProjectName");
    header->addWidget(name);
    m_updateBadge = createLabel(QString(), "badge", this);
    m_updateBadge->setAlignment(Qt::AlignCenter);
    m_updateBadge->setObjectName("ProjectUpdateBadge");
    header->addWidget(m_updateBadge);
    header->addStretch(1);
    body()->addLayout(header);

    auto* controls = new ProjectControls(project, context, mode, this);
    if (mode == ProjectControls::Mode::Library) {
        auto* details = new QPushButton(tr("Details"), this);
        details->setObjectName("ProjectDetails");
        details->setCursor(Qt::PointingHandCursor);
        connect(details, &QPushButton::clicked, this, [this] { emit detailsRequested(m_project.id); });
        controls->addLeadingButton(details);
    }
    body()->addWidget(controls);

    const auto refreshFor = [this](const QString& id) {
        if (id == m_project.id)
            refresh();
    };
    connect(&m_context.installed, &InstalledProjects::changed, this, refreshFor);
    connect(&m_context.updates, &ProjectUpdateManager::updateChanged, this, refreshFor);

    refresh();
}

void ProjectCard::refresh()
{
    // The badge: a newer version is known.
    const bool installed = m_context.installed.contains(m_project.id);
    const UpdateInfo update = m_context.updates.info(m_project.id);
    const bool updateKnown = installed && update.updateKnown() && update.state != UpdateState::UpToDate;
    const bool showBadge = updateKnown && update.state != UpdateState::Installing;
    m_updateBadge->setVisible(showBadge);
    if (showBadge)
        m_updateBadge->setText(tr("Update %1").arg(update.latestVersion));
}

} // namespace launcher::ui
