#include "ui/DashboardPage.h"

#include "app/Branding.h"
#include "github/RegistryManager.h"
#include "installer/InstallService.h"
#include "installer/InstalledProjects.h"
#include "process/ProcessManager.h"
#include "ui/Controls.h"
#include "ui/LauncherUpdatePanel.h"
#include "ui/Theme.h"
#include "updater/ProjectUpdateManager.h"

#include <QHBoxLayout>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher::ui {

namespace {

constexpr int kRecentCount = 5;
constexpr int kFeaturedCount = 4;

/// What an update that is under way is doing, for the line under the project's name.
QString updateStateText(UpdateState state)
{
    switch (state) {
    case UpdateState::Queued:
        return DashboardPage::tr("wartet in der Warteschlange");
    case UpdateState::Downloading:
        return DashboardPage::tr("wird geladen");
    case UpdateState::Verifying:
        return DashboardPage::tr("wird geprüft");
    case UpdateState::Pending:
        return DashboardPage::tr("vorgemerkt, bis das Projekt beendet ist");
    case UpdateState::Installing:
        return DashboardPage::tr("wird installiert");
    case UpdateState::Failed:
        return DashboardPage::tr("fehlgeschlagen");
    case UpdateState::Unknown:
    case UpdateState::UpToDate:
    case UpdateState::Available:
        break;
    }
    return DashboardPage::tr("verfügbar");
}

QString timeText(const QDateTime& time)
{
    return QLocale().toString(time.toLocalTime(), QLocale::ShortFormat);
}

} // namespace

DashboardPage::DashboardPage(const AppContext& context, QWidget* parent)
    : Page(tr("Dashboard"), tr("Dein Überblick über Projekte und Updates."), parent)
    , m_context(context)
{
    auto* welcome = new Card(this);
    auto* welcomeRow = new QHBoxLayout;
    welcomeRow->setSpacing(28);

    auto* welcomeTexts = new QVBoxLayout;
    welcomeTexts->setSpacing(6);
    welcomeTexts->addStretch(1);
    welcomeTexts->addWidget(
        createLabel(tr("Willkommen bei %1").arg(branding::productName()), "hero", welcome));
    QLabel* intro = createLabel(
        tr("Freigegebene Projekte entdecken, installieren, starten und aktuell halten. Alles an einem Ort."),
        "secondary", welcome);
    intro->setWordWrap(true);
    welcomeTexts->addWidget(intro);
    auto* toLibrary = new QPushButton(tr("Zur Bibliothek"), welcome);
    toLibrary->setObjectName("OpenLibrary");
    toLibrary->setCursor(Qt::PointingHandCursor);
    connect(toLibrary, &QPushButton::clicked, this, &DashboardPage::libraryRequested);
    welcomeTexts->addSpacing(6);
    welcomeTexts->addWidget(toLibrary, 0, Qt::AlignLeft);
    welcomeTexts->addStretch(1);
    welcomeRow->addLayout(welcomeTexts, 1);

    auto* logo = new QLabel(welcome);
    logo->setObjectName("WelcomeLogo");
    logo->setPixmap(Theme::brandImage(u"logo"_s, 104, devicePixelRatioF()));
    welcomeRow->addWidget(logo, 0, Qt::AlignVCenter);

    welcome->body()->addLayout(welcomeRow);
    contentLayout()->addWidget(welcome);

    contentLayout()->addSpacing(8);
    m_updates = createSection(tr("Verfügbare Updates"), "DashboardUpdates");
    contentLayout()->addSpacing(8);
    m_installed = createSection(tr("Installierte Projekte"), "DashboardInstalled");
    contentLayout()->addSpacing(8);
    m_recent = createSection(tr("Zuletzt gestartet"), "DashboardRecent");
    contentLayout()->addSpacing(8);
    m_featured = createSection(tr("Empfohlene Projekte"), "DashboardFeatured");

    contentLayout()->addSpacing(8);
    contentLayout()->addWidget(createLabel(tr("Launcher"), "section", this));

    auto* launcherCard = new Card(this);
    auto* versionRow = new QHBoxLayout;
    versionRow->addWidget(createLabel(tr("Installierte Version"), "body", launcherCard));
    versionRow->addStretch(1);
    QLabel* version = createLabel(branding::version(), "secondary", launcherCard);
    version->setObjectName("LauncherVersion");
    versionRow->addWidget(version);
    launcherCard->body()->addLayout(versionRow);
    auto* updates = new LauncherUpdatePanel(context.launcherUpdates, launcherCard);
    updates->setObjectName("DashboardLauncherUpdate");
    launcherCard->body()->addWidget(updates);
    contentLayout()->addWidget(launcherCard);

    contentLayout()->addStretch(1);

    // Several changes come in a burst (an installation ends, the list is saved, the status changes):
    // the lists are built once, after the last of them.
    m_rebuild.setSingleShot(true);
    m_rebuild.setInterval(0);
    connect(&m_rebuild, &QTimer::timeout, this, &DashboardPage::rebuildLists);
    connect(&m_context.installed, &InstalledProjects::changed, this, [this] { scheduleRebuild(); });
    connect(&m_context.updates, &ProjectUpdateManager::updateChanged, this, [this] { scheduleRebuild(); });
    connect(&m_context.registry, &RegistryManager::changed, this, [this] { scheduleRebuild(); });
    connect(&m_context.installService, &InstallService::statusChanged, this, [this] { scheduleRebuild(); });
    connect(&m_context.processes, &ProcessManager::runningChanged, this, [this] { scheduleRebuild(); });
    connect(&m_context.processes, &ProcessManager::exited, this, [this] { scheduleRebuild(); });

    rebuildLists();
}

DashboardPage::Section DashboardPage::createSection(const QString& title, const char* objectName)
{
    Section section;
    section.title = createLabel(title, "section", this);
    contentLayout()->addWidget(section.title);
    section.card = new Card(this);
    section.card->setObjectName(QLatin1StringView(objectName));
    section.body = section.card->body();
    contentLayout()->addWidget(section.card);
    return section;
}

QWidget* DashboardPage::createProjectRow(
    const QString& projectId, const QString& name, const QString& detail, bool withStart)
{
    auto* row = new QWidget;
    row->setObjectName("DashboardRow");
    row->setProperty("projectId", projectId);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(10);

    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    QLabel* nameLabel = createLabel(name, "title", row);
    nameLabel->setObjectName("DashboardRowName");
    texts->addWidget(nameLabel);
    QLabel* detailLabel = createLabel(detail, "secondary", row);
    detailLabel->setObjectName("DashboardRowDetail");
    detailLabel->setWordWrap(true);
    texts->addWidget(detailLabel);
    layout->addLayout(texts, 1);

    if (withStart) {
        auto* start = new QPushButton(tr("Starten"), row);
        start->setObjectName("DashboardStart");
        start->setCursor(Qt::PointingHandCursor);
        start->setProperty("accent", true);
        const bool running = m_context.processes.isRunning(projectId);
        const bool busy = m_context.installService.status(projectId).isBusy();
        start->setEnabled(!running && !busy);
        if (running)
            start->setText(tr("Läuft"));
        connect(start, &QPushButton::clicked, this, [this, projectId] {
            m_context.processes.clearLastExit(projectId);
            m_context.installService.start(projectId);
        });
        layout->addWidget(start, 0, Qt::AlignVCenter);
    }

    // The detail page shows what the list says about a project. An installed project that
    // has left the list has nothing to show, so it gets no button that would do nothing.
    const QList<Project>& listed = m_context.registry.projects();
    const bool inList = std::any_of(listed.cbegin(), listed.cend(),
        [&projectId](const Project& project) { return project.id == projectId; });
    if (inList) {
        auto* details = new QPushButton(tr("Details"), row);
        details->setObjectName("DashboardDetails");
        details->setCursor(Qt::PointingHandCursor);
        connect(
            details, &QPushButton::clicked, this, [this, projectId] { emit projectRequested(projectId); });
        layout->addWidget(details, 0, Qt::AlignVCenter);
    }
    return row;
}

void DashboardPage::fill(Section& section, const QList<QWidget*>& rows, const QString& emptyText)
{
    // The old rows are detached at once so that nothing finds them while they wait to be deleted.
    while (QLayoutItem* item = section.body->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }

    if (rows.isEmpty()) {
        QLabel* empty = createLabel(emptyText, "secondary", section.card);
        empty->setObjectName("DashboardEmpty");
        empty->setWordWrap(true);
        section.body->addWidget(empty);
        return;
    }
    for (qsizetype i = 0; i < rows.size(); ++i) {
        if (i > 0)
            section.body->addWidget(createDivider(section.card));
        rows.at(i)->setParent(section.card);
        section.body->addWidget(rows.at(i));
    }
}

void DashboardPage::rebuildLists()
{
    // The name a project goes by: the list's, or its id if the project is no longer in the list.
    const auto nameOf = [this](const QString& id) {
        for (const Project& project : m_context.registry.projects()) {
            if (project.id == id)
                return project.name;
        }
        return id;
    };

    QList<InstalledProject> installed = m_context.installed.all();
    std::sort(installed.begin(), installed.end(), [&](const InstalledProject& a, const InstalledProject& b) {
        return QString::localeAwareCompare(nameOf(a.projectId), nameOf(b.projectId)) < 0;
    });

    // --- available updates ---
    QList<QWidget*> updateRows;
    for (const InstalledProject& record : std::as_const(installed)) {
        const UpdateInfo update = m_context.updates.info(record.projectId);
        if (!update.updateKnown() || update.state == UpdateState::Unknown)
            continue;
        updateRows.append(createProjectRow(record.projectId, nameOf(record.projectId),
            tr("Version %1 → %2 · %3")
                .arg(record.version, update.latestVersion, updateStateText(update.state)),
            false));
    }
    const bool anyInstalled = !installed.isEmpty();
    m_updates.title->setVisible(anyInstalled);
    m_updates.card->setVisible(anyInstalled);
    fill(m_updates, updateRows, tr("Alle installierten Projekte sind aktuell."));

    // --- installed ---
    QList<QWidget*> installedRows;
    for (const InstalledProject& record : std::as_const(installed)) {
        const UpdateInfo update = m_context.updates.info(record.projectId);
        QString detail = tr("Version %1").arg(record.version);
        if (m_context.processes.isRunning(record.projectId))
            detail += tr(" · läuft");
        else if (update.state == UpdateState::UpToDate)
            detail += tr(" · aktuell");
        installedRows.append(createProjectRow(record.projectId, nameOf(record.projectId), detail, true));
    }
    fill(m_installed, installedRows,
        tr("Noch keine Projekte installiert. In der Bibliothek findest du alle freigegebenen Projekte."));

    // --- started lately ---
    QList<InstalledProject> started;
    for (const InstalledProject& record : std::as_const(installed)) {
        if (record.lastStartedAt.isValid())
            started.append(record);
    }
    std::sort(started.begin(), started.end(), [](const InstalledProject& a, const InstalledProject& b) {
        return a.lastStartedAt > b.lastStartedAt;
    });
    QList<QWidget*> recentRows;
    for (qsizetype i = 0; i < started.size() && i < kRecentCount; ++i) {
        const InstalledProject& record = started.at(i);
        recentRows.append(createProjectRow(record.projectId, nameOf(record.projectId),
            tr("Zuletzt gestartet: %1").arg(timeText(record.lastStartedAt)), true));
    }
    fill(m_recent, recentRows, tr("Noch kein Projekt gestartet."));

    // --- featured ---
    QList<Project> featured;
    for (const Project& project : m_context.registry.projects()) {
        if (project.featured)
            featured.append(project);
    }
    std::sort(featured.begin(), featured.end(),
        [](const Project& a, const Project& b) { return QString::localeAwareCompare(a.name, b.name) < 0; });
    QList<QWidget*> featuredRows;
    for (qsizetype i = 0; i < featured.size() && i < kFeaturedCount; ++i) {
        const Project& project = featured.at(i);
        const QString state =
            m_context.installed.contains(project.id) ? tr("installiert") : tr("nicht installiert");
        featuredRows.append(createProjectRow(project.id, project.name,
            u"%1 · %2 · %3"_s.arg(project.category, state, project.description), false));
    }
    m_featured.title->setVisible(!featuredRows.isEmpty());
    m_featured.card->setVisible(!featuredRows.isEmpty());
    fill(m_featured, featuredRows, QString());
}

} // namespace launcher::ui
