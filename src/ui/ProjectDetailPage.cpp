#include "ui/ProjectDetailPage.h"

#include "core/Format.h"
#include "core/SemVer.h"
#include "installer/InstallService.h"
#include "installer/InstalledProjects.h"
#include "ui/Controls.h"
#include "ui/FlowLayout.h"
#include "ui/OperationMessages.h"
#include "ui/ProjectControls.h"
#include "updater/ProjectUpdateManager.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace launcher::ui {

namespace {

/// A release received less than this long ago is not asked for again when the page opens.
constexpr qint64 kFreshSeconds = 10 * 60;

QLabel* createChip(const QString& text, const char* role, QWidget* parent)
{
    QLabel* chip = createLabel(text, role, parent);
    chip->setAlignment(Qt::AlignCenter);
    return chip;
}

/// A line "title … value": the name on the left, the value on the right.
QWidget* createFact(const QString& title, QLabel* value, QWidget* parent, QWidget* extra = nullptr)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(16);
    layout->addWidget(createLabel(title, "body", row));
    layout->addStretch(1);
    value->setParent(row);
    layout->addWidget(value);
    if (extra) {
        extra->setParent(row);
        layout->addWidget(extra);
    }
    return row;
}

} // namespace

ProjectDetailPage::ProjectDetailPage(const AppContext& context, QWidget* parent)
    : Page(tr("Projekt"), tr("Beschreibung, Version und Änderungen."), parent)
    , m_context(context)
{
    connect(&m_context.releases, &ReleaseManager::releaseReady, this,
        [this](const QString& id, const ReleaseSelection&) { onReleaseReady(id); });
    connect(&m_context.releases, &ReleaseManager::fetchFailed, this, &ProjectDetailPage::onReleaseFailed);

    // What the page shows about the installed version follows the managers.
    const auto versionsChanged = [this](const QString& id) {
        if (m_hasProject && id == m_project.id)
            refreshVersions();
    };
    connect(&m_context.installed, &InstalledProjects::changed, this, versionsChanged);
    connect(&m_context.updates, &ProjectUpdateManager::updateChanged, this, versionsChanged);
}

void ProjectDetailPage::clearContent()
{
    QLayout* layout = contentLayout();
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // Detached at once so that nothing finds the old widgets while they wait to be deleted.
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
    m_installedVersion = m_latestVersion = m_downloadSize = m_published = m_problem = nullptr;
    m_refresh = nullptr;
    m_notes = nullptr;
}

void ProjectDetailPage::showProject(const Project& project)
{
    m_project = project;
    m_hasProject = true;
    m_releaseFailure = {};
    rebuild();
    requestRelease(false);
}

void ProjectDetailPage::rebuild()
{
    clearContent();

    auto* back = new QPushButton(tr("← Zurück"), this);
    back->setObjectName("DetailBack");
    back->setCursor(Qt::PointingHandCursor);
    connect(back, &QPushButton::clicked, this, &ProjectDetailPage::backRequested);
    auto* backRow = new QHBoxLayout;
    backRow->addWidget(back);
    backRow->addStretch(1);
    contentLayout()->addLayout(backRow);

    // --- who and what ---
    auto* about = new Card(this);
    about->setObjectName("DetailAbout");
    auto* heading = new QHBoxLayout;
    heading->setSpacing(10);
    QLabel* name = createLabel(m_project.name, "hero", about);
    name->setObjectName("DetailName");
    heading->addWidget(name);
    if (m_project.featured) {
        QLabel* badge = createChip(tr("Empfohlen"), "badge", about);
        badge->setObjectName("DetailFeatured");
        heading->addWidget(badge);
    }
    heading->addStretch(1);
    QLabel* category = createLabel(m_project.category, "secondary", about);
    category->setObjectName("DetailCategory");
    heading->addWidget(category);
    about->body()->addLayout(heading);

    QLabel* description = createLabel(m_project.description, "body", about);
    description->setObjectName("DetailDescription");
    description->setWordWrap(true);
    about->body()->addWidget(description);

    if (!m_project.tags.isEmpty()) {
        auto* tags = new FlowLayout;
        for (const QString& tag : m_project.tags)
            tags->addWidget(createChip(tag, "chip", about));
        about->body()->addLayout(tags);
    }
    contentLayout()->addWidget(about);

    // --- versions ---
    contentLayout()->addWidget(createLabel(tr("Version"), "section", this));
    auto* versions = new Card(this);
    versions->setObjectName("DetailVersions");

    m_installedVersion = createLabel(QString(), "secondary", versions);
    m_installedVersion->setObjectName("DetailInstalledVersion");
    versions->body()->addWidget(createFact(tr("Installierte Version"), m_installedVersion, versions));

    m_latestVersion = createLabel(QString(), "secondary", versions);
    m_latestVersion->setObjectName("DetailLatestVersion");
    m_refresh = new QPushButton(tr("Neu abfragen"), versions);
    m_refresh->setObjectName("DetailRefresh");
    m_refresh->setCursor(Qt::PointingHandCursor);
    connect(m_refresh, &QPushButton::clicked, this, [this] { requestRelease(true); });
    versions->body()->addWidget(createFact(tr("Neueste Version"), m_latestVersion, versions, m_refresh));

    m_downloadSize = createLabel(QString(), "secondary", versions);
    m_downloadSize->setObjectName("DetailDownloadSize");
    versions->body()->addWidget(createFact(tr("Downloadgröße"), m_downloadSize, versions));

    m_published = createLabel(QString(), "secondary", versions);
    m_published->setObjectName("DetailPublished");
    versions->body()->addWidget(createFact(tr("Veröffentlicht"), m_published, versions));

    // The address is built from the two checked names of the registry entry, nothing else.
    const QString repository = u"%1/%2"_s.arg(m_project.owner, m_project.repository);
    QLabel* repositoryName = createLabel(repository, "secondary", versions);
    repositoryName->setObjectName("DetailRepository");
    auto* github = new QPushButton(tr("Auf GitHub öffnen"), versions);
    github->setObjectName("DetailGitHub");
    github->setCursor(Qt::PointingHandCursor);
    const QUrl url(u"https://github.com/%1"_s.arg(repository));
    github->setToolTip(url.toString());
    connect(github, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(url); });
    versions->body()->addWidget(createFact(tr("GitHub"), repositoryName, versions, github));

    m_problem = createLabel(QString(), "error", versions);
    m_problem->setObjectName("DetailProblem");
    m_problem->setWordWrap(true);
    versions->body()->addWidget(m_problem);
    contentLayout()->addWidget(versions);

    // --- what can be done ---
    contentLayout()->addWidget(createLabel(tr("Aktionen"), "section", this));
    auto* actions = new Card(this);
    actions->setObjectName("DetailActions");
    actions->body()->addWidget(new ProjectControls(m_project, m_context, actions));
    contentLayout()->addWidget(actions);

    // --- changelog ---
    contentLayout()->addWidget(createLabel(tr("Änderungen"), "section", this));
    auto* changelog = new Card(this);
    changelog->setObjectName("DetailChangelog");
    m_notes = new QPlainTextEdit(changelog);
    m_notes->setObjectName("DetailNotes");
    m_notes->setReadOnly(true);
    m_notes->setMinimumHeight(120);
    m_notes->setMaximumHeight(300);
    changelog->body()->addWidget(m_notes);
    contentLayout()->addWidget(changelog);

    contentLayout()->addStretch(1);
    refreshVersions();
}

void ProjectDetailPage::refreshVersions()
{
    if (!m_hasProject || !m_installedVersion)
        return;

    const std::optional<InstalledProject> record = m_context.installed.get(m_project.id);
    m_installedVersion->setText(record ? record->version : tr("Nicht installiert"));

    const std::optional<ReleaseSelection> latest = m_context.releases.latest(m_project.id);
    const bool fetching = m_context.releases.isFetching(m_project.id);
    m_refresh->setEnabled(!fetching);

    if (latest) {
        QString text = latest->release.version.toString();
        if (record) {
            const std::optional<SemVer> installed = SemVer::parse(record->version);
            if (installed && latest->release.version > *installed)
                text += tr(" · Update verfügbar");
            else if (installed && latest->release.version == *installed)
                text += tr(" · aktuell");
        }
        if (fetching)
            text += tr(" · wird neu abgefragt …");
        m_latestVersion->setText(text);
        m_downloadSize->setText(format::bytes(latest->asset.size));
        m_published->setText(latest->release.publishedAt.isValid()
                ? QLocale().toString(latest->release.publishedAt.toLocalTime(), QLocale::ShortFormat)
                : tr("unbekannt"));
        const QString notes = latest->release.notes.trimmed();
        m_notes->setPlainText(notes.isEmpty() ? tr("Zu diesem Release gibt es keine Beschreibung.") : notes);
    } else {
        m_latestVersion->setText(fetching ? tr("wird abgefragt …") : tr("unbekannt"));
        m_downloadSize->setText(u"–"_s);
        m_published->setText(u"–"_s);
        m_notes->setPlainText(fetching ? tr("Wird geladen …") : QString());
    }

    const QString problem = m_releaseFailure.isFailure() ? failureText(m_releaseFailure) : QString();
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());
}

void ProjectDetailPage::requestRelease(bool force)
{
    if (!m_hasProject || m_context.releases.isFetching(m_project.id))
        return;
    const QDateTime received = m_receivedAt.value(m_project.id);
    const bool fresh = received.isValid() && received.secsTo(QDateTime::currentDateTimeUtc()) < kFreshSeconds
        && m_context.releases.latest(m_project.id).has_value();
    if (!force && fresh)
        return;

    m_releaseFailure = {};
    m_context.releases.fetchLatest(m_project);
    refreshVersions();
}

void ProjectDetailPage::onReleaseReady(const QString& projectId)
{
    m_receivedAt.insert(projectId, QDateTime::currentDateTimeUtc());
    if (m_hasProject && projectId == m_project.id) {
        m_releaseFailure = {};
        refreshVersions();
    }
}

void ProjectDetailPage::onReleaseFailed(
    const QString& projectId, ReleaseManager::Problem problem, int httpStatus)
{
    if (!m_hasProject || projectId != m_project.id)
        return;
    m_releaseFailure = {};
    m_releaseFailure.kind = OperationFailure::Kind::Release;
    m_releaseFailure.code = static_cast<int>(problem);
    m_releaseFailure.httpStatus = httpStatus;
    refreshVersions();
}

} // namespace launcher::ui
