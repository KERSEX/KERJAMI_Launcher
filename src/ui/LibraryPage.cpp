#include "ui/LibraryPage.h"

#include "app/Branding.h"
#include "ui/Controls.h"
#include "ui/ProjectCard.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher::ui {

namespace {

using Problem = RegistryManager::Problem;
using Source = RegistryManager::Source;

/// Sorted, without duplicates that differ only in case.
QStringList uniqueSorted(const QStringList& values)
{
    QHash<QString, QString> byLowercase;
    for (const QString& value : values) {
        if (!byLowercase.contains(value.toLower()))
            byLowercase.insert(value.toLower(), value);
    }
    QStringList result = byLowercase.values();
    std::sort(result.begin(), result.end(),
        [](const QString& a, const QString& b) { return QString::localeAwareCompare(a, b) < 0; });
    return result;
}

/// Refills a filter box and keeps the user's choice if it is still available.
void fillFilter(ComboBox* box, const QString& allLabel, const QStringList& values)
{
    const QString current = box->currentData().toString();
    const QSignalBlocker blocker(box);
    box->clear();
    box->addItem(allLabel, QString());
    for (const QString& value : values)
        box->addItem(value, value);
    const int index = box->findData(current);
    box->setCurrentIndex(index >= 0 ? index : 0);
}

QString problemText(Problem problem, int httpStatus)
{
    switch (problem) {
    case Problem::None:
        break;
    case Problem::Unreachable:
        return LibraryPage::tr("Keine Verbindung zu GitHub.");
    case Problem::RateLimited:
        return LibraryPage::tr("GitHub nimmt vorübergehend keine weiteren Anfragen an.");
    case Problem::Http:
        if (httpStatus == 404)
            return LibraryPage::tr("Die Registry wurde auf GitHub nicht gefunden (HTTP 404).");
        return LibraryPage::tr("GitHub antwortete mit HTTP %1.").arg(httpStatus);
    case Problem::InvalidContent:
        return LibraryPage::tr("Die Registry enthält keine gültigen Daten.");
    case Problem::UnsupportedVersion:
        return LibraryPage::tr("Die Registry benötigt eine neuere Version von %1.")
            .arg(branding::productName());
    }
    return {};
}

QString timeText(const QDateTime& time)
{
    return time.isValid() ? QLocale().toString(time.toLocalTime(), QLocale::ShortFormat) : QString();
}

QString projectCountText(qsizetype count)
{
    return count == 1 ? LibraryPage::tr("1 Projekt") : LibraryPage::tr("%1 Projekte").arg(count);
}

} // namespace

LibraryPage::LibraryPage(RegistryManager& registry, QWidget* parent)
    : Page(tr("Bibliothek"), tr("Alle freigegebenen Projekte aus der offiziellen Registry."), parent)
    , m_registry(registry)
{
    auto* searchRow = new QHBoxLayout;
    searchRow->setSpacing(10);
    m_search = new QLineEdit(this);
    m_search->setObjectName("LibrarySearch");
    m_search->setPlaceholderText(tr("Projekte durchsuchen …"));
    m_search->setAccessibleName(tr("Suche"));
    searchRow->addWidget(m_search, 1);
    m_refresh = new QPushButton(tr("Aktualisieren"), this);
    m_refresh->setObjectName("LibraryRefresh");
    m_refresh->setCursor(Qt::PointingHandCursor);
    searchRow->addWidget(m_refresh);
    contentLayout()->addLayout(searchRow);

    auto* filterRow = new QHBoxLayout;
    filterRow->setSpacing(10);
    m_category = new ComboBox(this);
    m_category->setObjectName("LibraryCategory");
    m_category->setAccessibleName(tr("Kategorie"));
    filterRow->addWidget(m_category);
    m_tag = new ComboBox(this);
    m_tag->setObjectName("LibraryTag");
    m_tag->setAccessibleName(tr("Tag"));
    filterRow->addWidget(m_tag);
    filterRow->addSpacing(8);
    m_featuredOnly = new ToggleSwitch(this);
    m_featuredOnly->setObjectName("LibraryFeaturedOnly");
    m_featuredOnly->setAccessibleName(tr("Nur empfohlene Projekte"));
    filterRow->addWidget(m_featuredOnly);
    filterRow->addWidget(createLabel(tr("Nur Empfohlene"), "body", this));
    filterRow->addStretch(1);
    contentLayout()->addLayout(filterRow);

    m_status = createLabel(QString(), "secondary", this);
    m_status->setObjectName("LibraryStatus");
    m_status->setWordWrap(true);
    contentLayout()->addWidget(m_status);

    m_listHolder = new QWidget(this);
    m_listLayout = new QVBoxLayout(m_listHolder);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(12);
    contentLayout()->addWidget(m_listHolder);
    contentLayout()->addStretch(1);

    connect(m_search, &QLineEdit::textChanged, this, &LibraryPage::rebuildList);
    connect(m_category, &QComboBox::activated, this, &LibraryPage::rebuildList);
    connect(m_tag, &QComboBox::activated, this, &LibraryPage::rebuildList);
    connect(m_featuredOnly, &QAbstractButton::toggled, this, &LibraryPage::rebuildList);
    connect(m_refresh, &QPushButton::clicked, &m_registry, &RegistryManager::refresh);
    connect(&m_registry, &RegistryManager::changed, this, &LibraryPage::onRegistryChanged);

    onRegistryChanged();
}

void LibraryPage::onRegistryChanged()
{
    QStringList categories;
    QStringList tags;
    for (const Project& project : m_registry.projects()) {
        categories.append(project.category);
        tags.append(project.tags);
    }
    fillFilter(m_category, tr("Alle Kategorien"), uniqueSorted(categories));
    fillFilter(m_tag, tr("Alle Tags"), uniqueSorted(tags));

    m_refresh->setEnabled(!m_registry.isLoading());
    m_status->setText(statusText());
    rebuildList();
}

QString LibraryPage::statusText() const
{
    if (m_registry.isLoading())
        return tr("Registry wird geladen …");

    const QString counted = projectCountText(m_registry.projects().size());
    const QString time = timeText(m_registry.fetchedAt());

    if (m_registry.problem() != Problem::None) {
        // Without any list the error card below says the same, so it is not repeated here.
        if (m_registry.source() == Source::None)
            return {};
        return problemText(m_registry.problem(), m_registry.httpStatus()) + u' '
            + tr("Angezeigt wird die gespeicherte Liste vom %1 (%2).").arg(time, counted);
    }

    switch (m_registry.source()) {
    case Source::Network:
        return tr("Aktuell · %1 · geladen %2").arg(counted, time);
    case Source::Cache:
        return tr("Gespeicherte Liste vom %1 · %2").arg(time, counted);
    case Source::None:
        break;
    }
    return {};
}

QList<Project> LibraryPage::filteredProjects() const
{
    const QString needle = m_search->text().trimmed();
    const QString category = m_category->currentData().toString();
    const QString tag = m_tag->currentData().toString();
    const bool featuredOnly = m_featuredOnly->isChecked();

    QList<Project> result;
    for (const Project& project : m_registry.projects()) {
        if (featuredOnly && !project.featured)
            continue;
        if (!category.isEmpty() && project.category.compare(category, Qt::CaseInsensitive) != 0)
            continue;
        if (!tag.isEmpty() && !project.tags.contains(tag, Qt::CaseInsensitive))
            continue;
        if (!needle.isEmpty()) {
            const bool found = project.name.contains(needle, Qt::CaseInsensitive)
                || project.description.contains(needle, Qt::CaseInsensitive)
                || project.category.contains(needle, Qt::CaseInsensitive)
                || std::any_of(project.tags.cbegin(), project.tags.cend(),
                    [&needle](const QString& t) { return t.contains(needle, Qt::CaseInsensitive); });
            if (!found)
                continue;
        }
        result.append(project);
    }

    // Featured projects first, then alphabetical.
    std::stable_sort(result.begin(), result.end(), [](const Project& a, const Project& b) {
        if (a.featured != b.featured)
            return a.featured;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return result;
}

void LibraryPage::resetFilters()
{
    {
        const QSignalBlocker searchBlocker(m_search);
        const QSignalBlocker categoryBlocker(m_category);
        const QSignalBlocker tagBlocker(m_tag);
        const QSignalBlocker featuredBlocker(m_featuredOnly);
        m_search->clear();
        m_category->setCurrentIndex(0);
        m_tag->setCurrentIndex(0);
        m_featuredOnly->setChecked(false);
    }
    rebuildList();
}

void LibraryPage::rebuildList()
{
    while (QLayoutItem* item = m_listLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // Detached at once so that nothing finds the old cards while they wait to be deleted.
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }

    const auto addMessage = [this](char16_t glyph, const QString& title, const QString& text,
                                const QString& actionText, const char* actionName, auto onAction) {
        auto* card = new Card(m_listHolder);
        auto* state = new EmptyState(glyph, title, text, card);
        if (!actionText.isEmpty()) {
            QPushButton* button = state->addAction(actionText);
            button->setObjectName(QLatin1StringView(actionName));
            connect(button, &QPushButton::clicked, this, onAction);
        }
        card->body()->addWidget(state);
        m_listLayout->addWidget(card);
    };
    const auto loadAgain = [this] { m_registry.refresh(); };

    if (m_registry.projects().isEmpty()) {
        if (m_registry.isLoading()) {
            addMessage(
                Glyph::Library, tr("Registry wird geladen …"), tr("Einen Moment bitte."), {}, "", loadAgain);
        } else if (m_registry.source() == Source::None && m_registry.problem() != Problem::None) {
            addMessage(Glyph::Warning, tr("Projekte konnten nicht geladen werden"),
                problemText(m_registry.problem(), m_registry.httpStatus()), tr("Erneut versuchen"),
                "LibraryRetry", loadAgain);
        } else if (m_registry.source() == Source::None) {
            addMessage(Glyph::Library, tr("Registry noch nicht geladen"),
                tr("Die Projektliste wurde noch nicht von GitHub abgerufen."), tr("Jetzt laden"),
                "LibraryRetry", loadAgain);
        } else {
            addMessage(Glyph::Library, tr("Noch keine Projekte freigegeben"),
                tr("Sobald Projekte in die Registry aufgenommen werden, erscheinen sie hier."), {}, "",
                loadAgain);
        }
        return;
    }

    const QList<Project> projects = filteredProjects();
    if (projects.isEmpty()) {
        addMessage(Glyph::Library, tr("Keine Treffer"), tr("Zu den gewählten Filtern gibt es kein Projekt."),
            tr("Filter zurücksetzen"), "LibraryResetFilters", [this] { resetFilters(); });
        return;
    }

    for (const Project& project : projects)
        m_listLayout->addWidget(new ProjectCard(project, m_listHolder));
}

} // namespace launcher::ui
