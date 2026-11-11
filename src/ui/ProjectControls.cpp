#include "ui/ProjectControls.h"

#include "core/SettingsManager.h"
#include "installer/InstallService.h"
#include "installer/InstalledProjects.h"
#include "process/ProcessManager.h"
#include "ui/Dialogs.h"
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

/// A state in which an update of the project is under way and can be cancelled by the update manager.
bool updateUnderWay(UpdateState state)
{
    return state == UpdateState::Queued || state == UpdateState::Downloading
        || state == UpdateState::Verifying || state == UpdateState::Pending;
}

} // namespace

ProjectControls::ProjectControls(
    const Project& project, const AppContext& context, Mode mode, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
    , m_context(context)
    , m_mode(mode)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // What the user can do, and how it is going. In the narrow tile of the store the status line
    // gets a row of its own above the buttons; elsewhere it shares the row with them.
    m_actions = new QHBoxLayout;
    m_actions->setSpacing(10);
    m_state = createLabel(QString(), "secondary", this);
    m_state->setObjectName("ProjectState");
    m_state->setWordWrap(true);
    if (m_mode == Mode::Store) {
        layout->addWidget(m_state);
        m_actions->addStretch(1);
    } else {
        m_actions->addWidget(m_state, 1);
    }

    m_updateMode = new ComboBox(this);
    m_updateMode->setObjectName("ProjectUpdateMode");
    m_updateMode->setAccessibleName(tr("Updates"));
    m_updateMode->addItem(tr("Updates: Manuell"), static_cast<int>(UpdateMode::Manual));
    m_updateMode->addItem(tr("Updates: Automatisch"), static_cast<int>(UpdateMode::Automatic));
    m_actions->addWidget(m_updateMode, 0, Qt::AlignVCenter);

    m_uninstall = new QPushButton(tr("Deinstallieren"), this);
    m_uninstall->setObjectName("ProjectUninstall");
    m_uninstall->setCursor(Qt::PointingHandCursor);
    m_actions->addWidget(m_uninstall, 0, Qt::AlignVCenter);

    m_update = new QPushButton(this);
    m_update->setObjectName("ProjectUpdate");
    m_update->setCursor(Qt::PointingHandCursor);
    m_actions->addWidget(m_update, 0, Qt::AlignVCenter);

    m_primary = new QPushButton(this);
    m_primary->setObjectName("ProjectPrimary");
    m_primary->setCursor(Qt::PointingHandCursor);
    m_actions->addWidget(m_primary, 0, Qt::AlignVCenter);
    layout->addLayout(m_actions);

    m_progress = new QProgressBar(this);
    m_progress->setObjectName("ProjectProgress");
    m_progress->setTextVisible(false);
    m_progress->setRange(0, kProgressSteps);
    layout->addWidget(m_progress);

    m_problem = createLabel(QString(), "error", this);
    m_problem->setObjectName("ProjectProblem");
    m_problem->setWordWrap(true);
    layout->addWidget(m_problem);

    connect(m_primary, &QPushButton::clicked, this, &ProjectControls::onPrimaryClicked);
    connect(m_update, &QPushButton::clicked, this, &ProjectControls::onUpdateClicked);
    connect(m_uninstall, &QPushButton::clicked, this, &ProjectControls::onUninstallClicked);
    connect(m_updateMode, &QComboBox::activated, this, &ProjectControls::onUpdateModeChosen);

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

void ProjectControls::addLeadingButton(QPushButton* button)
{
    // After the status line (or the stretch that takes its place), before the update mode.
    button->setParent(this);
    m_actions->insertWidget(1, button, 0, Qt::AlignVCenter);
}

QString ProjectControls::problemText() const
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

void ProjectControls::refresh()
{
    const ProjectStatus status = m_context.installService.status(m_project.id);
    const std::optional<InstalledProject> record = m_context.installed.get(m_project.id);
    const UpdateInfo update = m_context.updates.info(m_project.id);
    const bool running = record && m_context.processes.isRunning(m_project.id);
    const bool queued = update.state == UpdateState::Queued;
    const bool busy = status.isBusy() || queued;
    const bool updateKnown = record && update.updateKnown() && update.state != UpdateState::UpToDate;

    // --- the main button ---
    // In the store an installed project has nothing to start: the status line says it is there.
    m_primary->setVisible(!(m_mode == Mode::Store && record && !busy));
    if (busy) {
        m_primary->setText(tr("Abbrechen"));
        m_primary->setProperty("accent", false);
        // Replacing the installed version cannot be stopped any more.
        m_primary->setEnabled(status.stage != ProjectStatus::Stage::Replacing);
    } else if (running && m_mode != Mode::Store) {
        m_primary->setText(tr("Läuft"));
        m_primary->setProperty("accent", false);
        m_primary->setEnabled(false);
    } else if (record && m_mode != Mode::Store) {
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
    // Removing and reconfiguring a project belong to its detail page, not to the lists.
    m_uninstall->setVisible(m_mode == Mode::Full && record.has_value() && !busy && !running);

    m_updateMode->setVisible(m_mode == Mode::Full && record.has_value());
    const int modeIndex =
        m_updateMode->findData(static_cast<int>(m_context.settings.updateMode(m_project.id)));
    if (modeIndex >= 0 && modeIndex != m_updateMode->currentIndex()) {
        const QSignalBlocker blocker(m_updateMode);
        m_updateMode->setCurrentIndex(modeIndex);
    }

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
    } else if (record && update.state == UpdateState::UpToDate) {
        m_state->setText(tr("Installiert: Version %1 · aktuell").arg(record->version));
    } else if (record) {
        m_state->setText(tr("Installiert: Version %1").arg(record->version));
    } else if (m_mode == Mode::Store) {
        m_state->setText({}); // the "Installieren" button says it all
    } else {
        m_state->setText(tr("Nicht installiert"));
    }
    m_state->setVisible(!m_state->text().isEmpty());

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

void ProjectControls::onPrimaryClicked()
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

void ProjectControls::onUpdateClicked()
{
    const UpdateInfo update = m_context.updates.info(m_project.id);
    if (update.state == UpdateState::Available && !Dialogs::confirmUpdate(this, m_project, update))
        return;
    m_context.updates.applyUpdate(m_project.id);
}

void ProjectControls::onUninstallClicked()
{
    if (!Dialogs::confirmUninstall(this, m_project))
        return;
    m_context.installService.uninstall(m_project.id);
}

void ProjectControls::onUpdateModeChosen(int index)
{
    const auto mode = static_cast<UpdateMode>(m_updateMode->itemData(index).toInt());
    m_context.settings.setUpdateMode(m_project.id, mode);
}

} // namespace launcher::ui
