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

QString projectCountText(qsizetype count)
{
    return count == 1 ? LibraryPage::tr("1 Projekt") : LibraryPage::tr("%1 Projekte").arg(count);
}

} // namespace

LibraryPage::LibraryPage(const AppContext& context, QWidget* parent)
    : Page(tr("Bibliothek"), tr("Alle freigegebenen Projekte."), parent)
    , m_context(context)
    , m_registry(context.registry)
{
    auto* searchRow = new QHBoxLayout;
    searchRow->setSpacing(10);
    m_search = new QLineEdit(this);
    m_search->setObjectName("LibrarySearch");
    m_search->setPlaceholderText(tr("Projekte durchsuchen …"));
    m_search->setAccessibleName(tr("Suche"));
    searchRow->addWidget(m_search, 1);
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

    m_status->setText(statusText());
    rebuildList();
}

QString LibraryPage::statusText() const
{
    // With no project the card below says what there is to say.
    if (m_registry.projects().isEmpty())
        return {};
    return projectCountText(m_registry.projects().size());
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

    if (m_registry.projects().isEmpty()) {
        if (!m_registry.error().isEmpty()) {
            // The list is part of the program, so this means a broken build.
            addMessage(Glyph::Warning, tr("Die Projektliste ist nicht lesbar"),
                tr("Installiere %1 bitte neu.").arg(branding::productName()), {}, "", [] { });
        } else {
            addMessage(Glyph::Library, tr("Noch keine Projekte freigegeben"),
                tr("Neue Projekte kommen mit einem Update von %1. Sobald eines freigegeben ist, erscheint es "
                   "hier.")
                    .arg(branding::productName()),
                {}, "", [] { });
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
        m_listLayout->addWidget(new ProjectCard(project, m_context, m_listHolder));
}

} // namespace launcher::ui
