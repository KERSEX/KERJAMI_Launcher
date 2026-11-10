#include "ui/LauncherUpdatePanel.h"

#include "ui/Controls.h"
#include "ui/Dialogs.h"
#include "ui/OperationMessages.h"
#include "updater/LauncherUpdateManager.h"

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

} // namespace

LauncherUpdatePanel::LauncherUpdatePanel(LauncherUpdateManager& manager, QWidget* parent)
    : QWidget(parent)
    , m_manager(manager)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* row = new QHBoxLayout;
    row->setSpacing(10);
    m_status = createLabel(QString(), "body", this);
    m_status->setObjectName("LauncherUpdateStatus");
    m_status->setWordWrap(true);
    row->addWidget(m_status, 1);

    m_cancel = new QPushButton(tr("Abbrechen"), this);
    m_cancel->setObjectName("LauncherUpdateCancel");
    m_check = new QPushButton(tr("Nach Updates suchen"), this);
    m_check->setObjectName("LauncherUpdateCheck");
    m_update = new QPushButton(tr("Aktualisieren"), this);
    m_update->setObjectName("LauncherUpdateInstall");
    m_restart = new QPushButton(tr("Jetzt neu starten"), this);
    m_restart->setObjectName("LauncherUpdateRestart");
    for (QPushButton* button : {m_cancel, m_check, m_update, m_restart}) {
        button->setCursor(Qt::PointingHandCursor);
        row->addWidget(button, 0, Qt::AlignVCenter);
    }
    layout->addLayout(row);

    m_progress = new QProgressBar(this);
    m_progress->setObjectName("LauncherUpdateProgress");
    m_progress->setTextVisible(false);
    m_progress->setRange(0, kProgressSteps);
    layout->addWidget(m_progress);

    m_problem = createLabel(QString(), "error", this);
    m_problem->setObjectName("LauncherUpdateProblem");
    m_problem->setWordWrap(true);
    layout->addWidget(m_problem);

    connect(m_check, &QPushButton::clicked, &m_manager, &LauncherUpdateManager::checkNow);
    connect(m_update, &QPushButton::clicked, this, &LauncherUpdatePanel::onUpdateClicked);
    connect(m_restart, &QPushButton::clicked, &m_manager, &LauncherUpdateManager::apply);
    connect(m_cancel, &QPushButton::clicked, &m_manager, &LauncherUpdateManager::cancel);
    connect(&m_manager, &LauncherUpdateManager::infoChanged, this, &LauncherUpdatePanel::refresh);

    refresh();
}

void LauncherUpdatePanel::refresh()
{
    const LauncherUpdateInfo info = m_manager.info();
    using State = LauncherUpdateState;

    m_status->setText(launcherUpdateStatusText(info));

    const bool idle = info.state == State::Idle || info.state == State::UpToDate
        || info.state == State::Available || info.state == State::Failed;
    const bool retry = info.state == State::Failed && info.updateKnown();

    m_check->setVisible(idle && !retry);
    m_update->setVisible(info.state == State::Available || retry);
    m_update->setText(retry ? tr("Erneut versuchen") : tr("Aktualisieren"));
    m_update->setProperty("accent", true);
    m_update->style()->unpolish(m_update);
    m_update->style()->polish(m_update);
    m_restart->setVisible(info.state == State::Ready);
    m_restart->setProperty("accent", true);
    m_restart->style()->unpolish(m_restart);
    m_restart->style()->polish(m_restart);
    m_cancel->setVisible(
        info.state == State::Downloading || info.state == State::Preparing || info.state == State::Ready);
    m_cancel->setEnabled(true);
    m_check->setEnabled(info.state != State::Checking);

    const bool showProgress = info.state == State::Downloading || info.state == State::Preparing;
    m_progress->setVisible(showProgress);
    if (showProgress) {
        if (info.total > 0) {
            m_progress->setRange(0, kProgressSteps);
            const qint64 scaled = info.done * kProgressSteps / info.total;
            m_progress->setValue(static_cast<int>(std::clamp<qint64>(scaled, 0, kProgressSteps)));
        } else {
            m_progress->setRange(0, 0);
        }
    }

    const QString problem = launcherUpdateFailureText(info.failure);
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());
}

void LauncherUpdatePanel::onUpdateClicked()
{
    const LauncherUpdateInfo info = m_manager.info();
    // After a failure the user has already agreed to this version; "Erneut versuchen" does not ask again.
    if (info.state == LauncherUpdateState::Available && !Dialogs::confirmLauncherUpdate(this, info))
        return;
    m_manager.install();
}

} // namespace launcher::ui
