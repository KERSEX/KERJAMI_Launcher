#include "ui/ProjectCard.h"

#include "ui/Dialogs.h"
#include "ui/FlowLayout.h"
#include "ui/OperationMessages.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher::ui {

namespace {

constexpr int kProgressSteps = 1000;

QLabel* createChip(const QString& text, const char* role, QWidget* parent)
{
    QLabel* chip = createLabel(text, role, parent);
    chip->setAlignment(Qt::AlignCenter);
    return chip;
}

} // namespace

ProjectCard::ProjectCard(
    const Project& project, InstallService& service, InstalledProjects& installed, QWidget* parent)
    : Card(parent)
    , m_project(project)
    , m_service(service)
    , m_installed(installed)
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

    // What the user can do, and how it is going.
    auto* actions = new QHBoxLayout;
    actions->setSpacing(10);
    m_state = createLabel(QString(), "secondary", this);
    m_state->setObjectName("ProjectState");
    m_state->setWordWrap(true);
    actions->addWidget(m_state, 1);

    m_uninstall = new QPushButton(tr("Deinstallieren"), this);
    m_uninstall->setObjectName("ProjectUninstall");
    m_uninstall->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_uninstall, 0, Qt::AlignVCenter);

    m_primary = new QPushButton(this);
    m_primary->setObjectName("ProjectPrimary");
    m_primary->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_primary, 0, Qt::AlignVCenter);
    body()->addLayout(actions);

    m_progress = new QProgressBar(this);
    m_progress->setObjectName("ProjectProgress");
    m_progress->setTextVisible(false);
    m_progress->setRange(0, kProgressSteps);
    body()->addWidget(m_progress);

    m_problem = createLabel(QString(), "error", this);
    m_problem->setObjectName("ProjectProblem");
    m_problem->setWordWrap(true);
    body()->addWidget(m_problem);

    connect(m_primary, &QPushButton::clicked, this, &ProjectCard::onPrimaryClicked);
    connect(m_uninstall, &QPushButton::clicked, this, &ProjectCard::onUninstallClicked);
    connect(&m_service, &InstallService::statusChanged, this, [this](const QString& id) {
        if (id == m_project.id)
            refresh();
    });
    connect(&m_installed, &InstalledProjects::changed, this, [this](const QString& id) {
        if (id == m_project.id)
            refresh();
    });

    refresh();
}

void ProjectCard::refresh()
{
    const ProjectStatus status = m_service.status(m_project.id);
    const std::optional<InstalledProject> record = m_installed.get(m_project.id);
    const OperationFailure failure = m_service.lastFailure(m_project.id);
    const bool busy = status.isBusy();

    if (busy) {
        m_primary->setText(tr("Abbrechen"));
        m_primary->setProperty("accent", false);
        // Replacing the installed version cannot be stopped any more.
        m_primary->setEnabled(status.stage != ProjectStatus::Stage::Replacing);
    } else if (record) {
        m_primary->setText(tr("Starten"));
        m_primary->setProperty("accent", true);
        m_primary->setEnabled(true);
    } else {
        m_primary->setText(tr("Installieren"));
        m_primary->setProperty("accent", true);
        m_primary->setEnabled(true);
    }
    // Qt reads a changed property only after the style is applied again.
    m_primary->style()->unpolish(m_primary);
    m_primary->style()->polish(m_primary);

    m_uninstall->setVisible(record.has_value() && !busy);

    if (busy) {
        m_state->setText(stageText(status));
    } else if (record) {
        m_state->setText(tr("Installiert: Version %1").arg(record->version));
    } else {
        m_state->setText(tr("Nicht installiert"));
    }

    m_progress->setVisible(busy);
    if (busy) {
        if (status.total > 0) {
            m_progress->setRange(0, kProgressSteps);
            const qint64 scaled = status.done * kProgressSteps / status.total;
            m_progress->setValue(static_cast<int>(std::clamp<qint64>(scaled, 0, kProgressSteps)));
        } else {
            m_progress->setRange(0, 0); // no size known: moving bar
        }
    }

    const QString problem = failureText(failure);
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());
}

void ProjectCard::onPrimaryClicked()
{
    const ProjectStatus status = m_service.status(m_project.id);
    if (status.isBusy()) {
        m_service.cancel(m_project.id);
    } else if (m_installed.contains(m_project.id)) {
        m_service.start(m_project.id);
    } else {
        m_service.install(m_project);
    }
}

void ProjectCard::onUninstallClicked()
{
    if (!Dialogs::confirmUninstall(this, m_project))
        return;
    m_service.uninstall(m_project.id);
}

} // namespace launcher::ui
