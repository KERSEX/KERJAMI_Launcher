#include "ui/StorePage.h"

#include "app/Branding.h"
#include "ui/Controls.h"
#include "ui/FlowLayout.h"
#include "ui/StoreTile.h"
#include "ui/Theme.h"

#include <QButtonGroup>
#include <QHash>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
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

QString projectCountText(qsizetype count)
{
    return count == 1 ? StorePage::tr("1 Projekt") : StorePage::tr("%1 Projekte").arg(count);
}

/// Removes everything from a layout; the widgets go away once the event loop is idle.
void clearLayout(QLayout* layout)
{
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // Detached at once so that nothing finds the old widgets while they wait to be deleted.
            widget->setParent(nullptr);
            widget->deleteLater();
        }
        delete item;
    }
}

} // namespace

StorePage::StorePage(const AppContext& context, QWidget* parent)
    : Page(tr("Store"), tr("Entdecke Projekte und installiere sie mit einem Klick."), parent)
    , m_context(context)
    , m_registry(context.registry)
{
    m_search = new QLineEdit(this);
    m_search->setObjectName("StoreSearch");
    m_search->setPlaceholderText(tr("Im Store suchen …"));
    m_search->setAccessibleName(tr("Suche"));
    contentLayout()->addWidget(m_search);

    // One button per category, one of them checked; "Alle" is always the first.
    m_categoryHolder = new QWidget(this);
    m_categoryHolder->setObjectName("StoreCategories");
    m_categoryLayout = new FlowLayout(m_categoryHolder, 8, 8);
    m_categories = new QButtonGroup(this);
    m_categories->setExclusive(true);
    contentLayout()->addWidget(m_categoryHolder);

    m_status = createLabel(QString(), "secondary", this);
    m_status->setObjectName("StoreStatus");
    m_status->setWordWrap(true);
    contentLayout()->addWidget(m_status);

    m_messageHolder = new QWidget(this);
    m_messageLayout = new QVBoxLayout(m_messageHolder);
    m_messageLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout()->addWidget(m_messageHolder);

    m_tileHolder = new QWidget(this);
    m_tileHolder->setObjectName("StoreTiles");
    m_tileLayout = new FlowLayout(m_tileHolder, 14, 14);
    m_tileLayout->setEqualRowHeight(true); // tiles of a row line up, whatever one of them has to say
    contentLayout()->addWidget(m_tileHolder);
    contentLayout()->addStretch(1);

    connect(m_search, &QLineEdit::textChanged, this, &StorePage::rebuildTiles);
    connect(m_categories, &QButtonGroup::buttonClicked, this, &StorePage::rebuildTiles);
    connect(&m_registry, &RegistryManager::changed, this, &StorePage::onRegistryChanged);

    onRegistryChanged();
}

QString StorePage::chosenCategory() const
{
    const QAbstractButton* checked = m_categories->checkedButton();
    return checked ? checked->property("category").toString() : QString();
}

void StorePage::onRegistryChanged()
{
    rebuildCategories();
    rebuildTiles();
}

void StorePage::rebuildCategories()
{
    const QString before = chosenCategory();

    for (QAbstractButton* button : m_categories->buttons())
        m_categories->removeButton(button);
    clearLayout(m_categoryLayout);

    QStringList names;
    for (const Project& project : m_registry.projects())
        names.append(project.category);
    names = uniqueSorted(names);

    const auto addChip = [&](const QString& text, const QString& category) {
        auto* chip = new QPushButton(text, m_categoryHolder);
        chip->setObjectName("StoreCategory");
        chip->setProperty("categoryChip", true);
        chip->setProperty("category", category);
        chip->setCheckable(true);
        chip->setCursor(Qt::PointingHandCursor);
        m_categories->addButton(chip);
        m_categoryLayout->addWidget(chip);
        return chip;
    };
    QAbstractButton* all = addChip(tr("Alle"), QString());
    QAbstractButton* chosen = all;
    for (const QString& name : std::as_const(names)) {
        QAbstractButton* chip = addChip(name, name);
        if (name.compare(before, Qt::CaseInsensitive) == 0)
            chosen = chip;
    }
    chosen->setChecked(true);

    // A single category (or none) leaves nothing to choose.
    m_categoryHolder->setVisible(names.size() > 1);
}

QList<Project> StorePage::filteredProjects() const
{
    const QString needle = m_search->text().trimmed();
    const QString category = m_categoryHolder->isHidden() ? QString() : chosenCategory();

    QList<Project> result;
    for (const Project& project : m_registry.projects()) {
        if (!category.isEmpty() && project.category.compare(category, Qt::CaseInsensitive) != 0)
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

void StorePage::resetFilters()
{
    {
        const QSignalBlocker searchBlocker(m_search);
        m_search->clear();
    }
    if (QAbstractButton* all = m_categories->buttons().value(0))
        all->setChecked(true);
    rebuildTiles();
}

void StorePage::rebuildTiles()
{
    clearLayout(m_messageLayout);
    clearLayout(m_tileLayout);

    const auto addMessage = [this](char16_t glyph, const QString& title, const QString& text,
                                const QString& actionText, const char* actionName, auto onAction) {
        auto* card = new Card(m_messageHolder);
        auto* state = new EmptyState(glyph, title, text, card);
        if (!actionText.isEmpty()) {
            QPushButton* button = state->addAction(actionText);
            button->setObjectName(QLatin1StringView(actionName));
            connect(button, &QPushButton::clicked, this, onAction);
        }
        card->body()->addWidget(state);
        m_messageLayout->addWidget(card);
    };
    m_messageHolder->setVisible(false);
    m_tileHolder->setVisible(true);

    if (m_registry.projects().isEmpty()) {
        m_status->setText({});
        m_messageHolder->setVisible(true);
        m_tileHolder->setVisible(false);
        if (!m_registry.error().isEmpty()) {
            // The list is part of the program, so this means a broken build.
            addMessage(Glyph::Warning, tr("Die Projektliste ist nicht lesbar"),
                tr("Installiere %1 bitte neu.").arg(branding::productName()), {}, "", [] { });
        } else {
            addMessage(Glyph::Store, tr("Noch keine Projekte im Store"),
                tr("Neue Projekte kommen mit einem Update von %1. Sobald eines freigegeben ist, erscheint es "
                   "hier.")
                    .arg(branding::productName()),
                {}, "", [] { });
        }
        return;
    }

    const QList<Project> projects = filteredProjects();
    m_status->setText(projectCountText(projects.size()));
    if (projects.isEmpty()) {
        m_messageHolder->setVisible(true);
        m_tileHolder->setVisible(false);
        addMessage(Glyph::Store, tr("Keine Treffer"), tr("Zu Suche und Kategorie gibt es kein Projekt."),
            tr("Filter zurücksetzen"), "StoreResetFilters", [this] { resetFilters(); });
        return;
    }

    for (const Project& project : projects) {
        auto* tile = new StoreTile(project, m_context, m_tileHolder);
        connect(tile, &StoreTile::detailsRequested, this, &StorePage::projectDetailsRequested);
        m_tileLayout->addWidget(tile);
    }
}

} // namespace launcher::ui
