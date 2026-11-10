#include "ui/ProjectCard.h"

#include "core/SettingsManager.h"
#include "installer/InstalledProjects.h"
#include "process/ProcessManager.h"
#include "ui/Dialogs.h"
#include "ui/FlowLayout.h"
#include "ui/OperationMessages.h"
#include "updater/ProjectUpdateManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
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

/// A state in which an update of the project is under way and can be cancelled by the update manager.
bool updateUnderWay(UpdateState state)
{
    return state == UpdateState::Queued || state == UpdateState::Downloading
        || state == UpdateState::Verifying || state == UpdateState::Pending;
}

} // namespace

ProjectCard::ProjectCard(const Project& project, const AppContext& context, QWidget* parent)
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
    if (project.featured) {
        QLabel* badge = createChip(tr("Empfohlen"), "badge", this);
        badge->setObjectName("ProjectFeatured");
        header->addWidget(badge);
    }
    m_updateBadge = createChip(QString(), "badge", this);
    m_updateBadge->setObjectName("ProjectUpdateBadge");
    header->addWidget(m_updateBadge);
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

    m_updateMode = new ComboBox(this);
    m_updateMode->setObjectName("ProjectUpdateMode");
    m_updateMode->setAccessibleName(tr("Updates"));
    m_updateMode->addItem(tr("Updates: Manuell"), static_cast<int>(UpdateMode::Manual));
    m_updateMode->addItem(tr("Updates: Automatisch"), static_cast<int>(UpdateMode::Automatic));
    actions->addWidget(m_updateMode, 0, Qt::AlignVCenter);

    m_uninstall = new QPushButton(tr("Deinstallieren"), this);
    m_uninstall->setObjectName("ProjectUninstall");
    m_uninstall->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_uninstall, 0, Qt::AlignVCenter);

    m_update = new QPushButton(this);
    m_update->setObjectName("ProjectUpdate");
    m_update->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_update, 0, Qt::AlignVCenter);

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
    connect(m_update, &QPushButton::clicked, this, &ProjectCard::onUpdateClicked);
    connect(m_uninstall, &QPushButton::clicked, this, &ProjectCard::onUninstallClicked);
    connect(m_updateMode, &QComboBox::activated, this, &ProjectCard::onUpdateModeChosen);

    const auto refreshFor = [this](const QString& id) {
        if (id == m_project.id)
            refresh();
    };
    connect(&m_context.installService, &InstallService::statusChanged, this, refreshFor);
    connect(&m_context.installed, &InstalledProjects::changed, this, refreshFor);
    connect(&m_context.updates, &ProjectUpdateManager::updateChanged, this, refreshFor);
    connect(&m_context.processes, &ProcessManager::runningChanged, this,
        [refreshFor](const QString& id, bool) { refreshFor(id); });
    connect(&m_context.processes, &ProcessManager::exited, this,
        [refreshFor](const QString& id, const ProcessExit&) { refreshFor(id); });
    connect(&m_context.settings, &SettingsManager::projectSettingsChanged, this, refreshFor);

    refresh();
}

QString ProjectCard::problemText() const
{
    const OperationFailure failure = m_context.installService.lastFailure(m_project.id);
    if (failure.isFailure())
        return failureText(failure);

    const UpdateInfo update = m_context.updates.info(m_project.id);
    if (update.state == UpdateState::Failed && update.failure.isFailure())
        return tr(
            "Das Update auf Version %1 ist fehlgeschlagen. Die installierte Version ist unverändert. %2")
            .arg(update.latestVersion, failureText(update.failure));

    if (const std::optional<ProcessExit> exit = m_context.processes.lastExit(m_project.id);
        exit && exit->unexpected())
        return exitText(*exit);
    return {};
}

void ProjectCard::refresh()
{
    const ProjectStatus status = m_context.installService.status(m_project.id);
    const std::optional<InstalledProject> record = m_context.installed.get(m_project.id);
    const UpdateInfo update = m_context.updates.info(m_project.id);
    const bool running = record && m_context.processes.isRunning(m_project.id);
    const bool queued = update.state == UpdateState::Queued;
    const bool busy = status.isBusy() || queued;
    const bool updateKnown = record && update.updateKnown() && update.state != UpdateState::UpToDate;

    // --- the main button ---
    if (busy) {
        m_primary->setText(tr("Abbrechen"));
        m_primary->setProperty("accent", false);
        // Replacing the installed version cannot be stopped any more.
        m_primary->setEnabled(status.stage != ProjectStatus::Stage::Replacing);
    } else if (running) {
        m_primary->setText(tr("Läuft"));
        m_primary->setProperty("accent", false);
        m_primary->setEnabled(false);
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

    // --- update, uninstall, update mode ---
    const bool canUpdate = record && !busy
        && (update.state == UpdateState::Available || update.state == UpdateState::Failed) && updateKnown;
    m_update->setVisible(canUpdate);
    m_update->setText(update.state == UpdateState::Failed ? tr("Erneut versuchen") : tr("Aktualisieren"));
    m_uninstall->setVisible(record.has_value() && !busy && !running);

    m_updateMode->setVisible(record.has_value());
    const int modeIndex =
        m_updateMode->findData(static_cast<int>(m_context.settings.updateMode(m_project.id)));
    if (modeIndex >= 0 && modeIndex != m_updateMode->currentIndex()) {
        const QSignalBlocker blocker(m_updateMode);
        m_updateMode->setCurrentIndex(modeIndex);
    }

    // --- badge: a newer version is known ---
    const bool showBadge = updateKnown && update.state != UpdateState::Installing;
    m_updateBadge->setVisible(showBadge);
    if (showBadge)
        m_updateBadge->setText(tr("Update %1").arg(update.latestVersion));

    // --- the status line ---
    if (queued && !status.isBusy()) {
        m_state->setText(tr("Update auf Version %1 wartet in der Warteschlange.").arg(update.latestVersion));
    } else if (status.stage == ProjectStatus::Stage::Prepared) {
        m_state->setText(
            tr("Update auf Version %1 ist vorgemerkt und wird installiert, sobald %2 beendet ist.")
                .arg(update.latestVersion, m_project.name));
    } else if (busy) {
        m_state->setText(stageText(status));
    } else if (running) {
        m_state->setText(tr("Installiert: Version %1 · läuft").arg(record->version));
    } else if (record) {
        m_state->setText(tr("Installiert: Version %1").arg(record->version));
    } else {
        m_state->setText(tr("Nicht installiert"));
    }

    // --- progress ---
    m_progress->setVisible(status.isBusy() && status.stage != ProjectStatus::Stage::Prepared);
    if (status.isBusy()) {
        if (status.total > 0) {
            m_progress->setRange(0, kProgressSteps);
            const qint64 scaled = status.done * kProgressSteps / status.total;
            m_progress->setValue(static_cast<int>(std::clamp<qint64>(scaled, 0, kProgressSteps)));
        } else {
            m_progress->setRange(0, 0); // no size known: moving bar
        }
    }

    const QString problem = problemText();
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());
}

void ProjectCard::onPrimaryClicked()
{
    const UpdateInfo update = m_context.updates.info(m_project.id);
    const ProjectStatus status = m_context.installService.status(m_project.id);
    if (updateUnderWay(update.state)) {
        m_context.updates.cancel(m_project.id);
    } else if (status.isBusy()) {
        m_context.installService.cancel(m_project.id);
    } else if (m_context.installed.contains(m_project.id)) {
        m_context.processes.clearLastExit(m_project.id);
        m_context.installService.start(m_project.id);
    } else {
        m_context.installService.install(m_project);
    }
}

void ProjectCard::onUpdateClicked()
{
    const UpdateInfo update = m_context.updates.info(m_project.id);
    if (update.state == UpdateState::Available && !Dialogs::confirmUpdate(this, m_project, update))
        return;
    m_context.updates.applyUpdate(m_project.id);
}

void ProjectCard::onUninstallClicked()
{
    if (!Dialogs::confirmUninstall(this, m_project))
        return;
    m_context.installService.uninstall(m_project.id);
}

void ProjectCard::onUpdateModeChosen(int index)
{
    const auto mode = static_cast<UpdateMode>(m_updateMode->itemData(index).toInt());
    m_context.settings.setUpdateMode(m_project.id, mode);
}

} // namespace launcher::ui
