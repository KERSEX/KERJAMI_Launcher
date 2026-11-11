#include "ui/LibraryPage.h"

#include "installer/InstalledProjects.h"
#include "ui/Controls.h"
#include "ui/ProjectCard.h"
#include "ui/Theme.h"

#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher::ui {

LibraryPage::LibraryPage(const AppContext& context, QWidget* parent)
    : Page(tr("Bibliothek"), tr("Deine installierten Projekte."), parent)
    , m_context(context)
{
    m_status = createLabel(QString(), "secondary", this);
    m_status->setObjectName("LibraryStatus");
    contentLayout()->addWidget(m_status);

    m_listHolder = new QWidget(this);
    m_listLayout = new QVBoxLayout(m_listHolder);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(12);
    contentLayout()->addWidget(m_listHolder);
    contentLayout()->addStretch(1);

    // Several changes come in a burst (an installation ends, the list is saved): the list is
    // built once, after the last of them.
    m_rebuild.setSingleShot(true);
    m_rebuild.setInterval(0);
    connect(&m_rebuild, &QTimer::timeout, this, &LibraryPage::rebuildList);
    connect(&m_context.installed, &InstalledProjects::changed, this, [this] { m_rebuild.start(); });
    connect(&m_context.registry, &RegistryManager::changed, this, [this] { m_rebuild.start(); });

    rebuildList();
}

Project LibraryPage::projectFor(const QString& projectId, bool* listed) const
{
    for (const Project& project : m_context.registry.projects()) {
        if (project.id == projectId) {
            *listed = true;
            return project;
        }
    }

    // The project has left the list. What is known of it is enough to start and remove it.
    *listed = false;
    Project project;
    project.id = projectId;
    project.name = projectId;
    if (const std::optional<InstalledProject> record = m_context.installed.get(projectId)) {
        project.installDirectory = record->installDirectory;
        project.executable = record->executable;
    }
    return project;
}

void LibraryPage::rebuildList()
{
    struct Entry {
        Project project;
        bool listed = false;
    };
    QList<Entry> entries;
    for (const InstalledProject& record : m_context.installed.all()) {
        Entry entry;
        entry.project = projectFor(record.projectId, &entry.listed);
        entries.append(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return QString::localeAwareCompare(a.project.name, b.project.name) < 0;
    });

    // The cards follow the managers by themselves (version, update, running). The list is only
    // built again when what it shows changes: a project comes or goes, or its entry does. A start,
    // which is recorded as a change too, leaves the cards alone.
    QStringList signature;
    for (const Entry& entry : std::as_const(entries))
        signature.append(
            u"%1|%2|%3"_s.arg(entry.project.id, entry.project.name, entry.listed ? u"1"_s : u"0"_s));
    if (m_built && signature == m_signature)
        return;
    m_built = true;
    m_signature = signature;

    while (QLayoutItem* item = m_listLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // Detached at once so that nothing finds the old cards while they wait to be deleted.
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }

    if (entries.isEmpty()) {
        m_status->setText({});
        auto* card = new Card(m_listHolder);
        auto* state = new EmptyState(Glyph::Library, tr("Noch nichts installiert"),
            tr("Im Store findest du alle freigegebenen Projekte."), card);
        QPushButton* toStore = state->addAction(tr("Zum Store"));
        toStore->setObjectName("LibraryOpenStore");
        connect(toStore, &QPushButton::clicked, this, &LibraryPage::storeRequested);
        card->body()->addWidget(state);
        m_listLayout->addWidget(card);
        return;
    }

    m_status->setText(entries.size() == 1 ? tr("1 Projekt installiert")
                                          : tr("%1 Projekte installiert").arg(entries.size()));
    for (const Entry& entry : std::as_const(entries)) {
        auto* card = new ProjectCard(entry.project, m_context,
            entry.listed ? ProjectControls::Mode::Library : ProjectControls::Mode::Full, m_listHolder);
        connect(card, &ProjectCard::detailsRequested, this, &LibraryPage::projectDetailsRequested);
        m_listLayout->addWidget(card);
    }
}

} // namespace launcher::ui
