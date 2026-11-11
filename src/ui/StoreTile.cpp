#include "ui/StoreTile.h"

#include "ui/ProjectControls.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher::ui {

namespace {

// Three tiles side by side fit in the content of the default window, two in that of the smallest.
constexpr int kTileWidth = 296;
constexpr int kDescriptionLines = 3;
constexpr int kDescriptionChars = 110;

} // namespace

StoreTile::StoreTile(const Project& project, const AppContext& context, QWidget* parent)
    : Card(parent)
    , m_project(project)
{
    setObjectName("StoreTile");
    setFixedWidth(kTileWidth);
    setCursor(Qt::PointingHandCursor);
    body()->setSpacing(6);

    auto* header = new QHBoxLayout;
    header->setSpacing(8);
    QLabel* name = createLabel(project.name, "title", this);
    name->setObjectName("StoreTileName");
    header->addWidget(name);
    if (project.featured) {
        QLabel* badge = createLabel(tr("Empfohlen"), "badge", this);
        badge->setAlignment(Qt::AlignCenter);
        badge->setObjectName("StoreTileFeatured");
        header->addWidget(badge);
    }
    header->addStretch(1);
    body()->addLayout(header);

    QLabel* category = createLabel(project.category, "chip", this);
    category->setObjectName("StoreTileCategory");
    category->setAlignment(Qt::AlignCenter);
    auto* categoryRow = new QHBoxLayout;
    categoryRow->addWidget(category);
    categoryRow->addStretch(1);
    body()->addLayout(categoryRow);

    // A fixed height of a few lines keeps the tiles of a row equally tall, whatever the text.
    QLabel* description = createLabel(shortDescription(project.description), "secondary", this);
    description->setObjectName("StoreTileDescription");
    description->setWordWrap(true);
    description->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    description->setFixedHeight(description->fontMetrics().lineSpacing() * kDescriptionLines + 4);
    body()->addWidget(description);
    // When the row is taller than this tile (a neighbour shows a message), the buttons stay at the bottom.
    body()->addStretch(1);

    auto* controls = new ProjectControls(project, context, ProjectControls::Mode::Store, this);
    auto* details = new QPushButton(tr("Details"), this);
    details->setObjectName("StoreTileDetails");
    details->setCursor(Qt::PointingHandCursor);
    connect(details, &QPushButton::clicked, this, [this] { emit detailsRequested(m_project.id); });
    controls->addLeadingButton(details);
    body()->addWidget(controls);
}

QSize StoreTile::sizeHint() const
{
    // The height depends on the width (a status line or a problem may wrap), which a layout
    // that places the tiles by their size hint does not ask for; the tile answers for itself.
    QLayout* inner = layout();
    const int height =
        inner->hasHeightForWidth() ? inner->heightForWidth(kTileWidth) : inner->sizeHint().height();
    return {kTileWidth, std::max(height, inner->minimumSize().height())};
}

QString StoreTile::shortDescription(const QString& text)
{
    const QString plain = text.simplified();
    if (plain.size() <= kDescriptionChars)
        return plain;
    QString cut = plain.left(kDescriptionChars);
    const qsizetype space = cut.lastIndexOf(u' ');
    if (space > kDescriptionChars / 2)
        cut.truncate(space);
    while (!cut.isEmpty() && (cut.endsWith(u'.') || cut.endsWith(u',') || cut.endsWith(u' ')))
        cut.chop(1);
    return cut + u"…"_s;
}

void StoreTile::mouseReleaseEvent(QMouseEvent* event)
{
    // Buttons take their own clicks; what reaches the tile is a click on its text or its empty parts.
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit detailsRequested(m_project.id);
    Card::mouseReleaseEvent(event);
}

} // namespace launcher::ui
