#include "ui/FlowLayout.h"

#include <QWidget>

#include <algorithm>

namespace launcher::ui {

FlowLayout::FlowLayout(QWidget* parent, int horizontalSpacing, int verticalSpacing)
    : QLayout(parent)
    , m_horizontalSpacing(horizontalSpacing)
    , m_verticalSpacing(verticalSpacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    qDeleteAll(m_items);
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_items.append(item);
}

int FlowLayout::count() const
{
    return static_cast<int>(m_items.size());
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return m_items.value(index, nullptr);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* item : m_items)
        size = size.expandedTo(item->minimumSize());
    const QMargins margins = contentsMargins();
    return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    const QMargins margins = contentsMargins();
    const QRect area = rect.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());

    struct Placed {
        QLayoutItem* item;
        QPoint position;
        QSize size;
    };
    QList<Placed> row;
    int x = area.x();
    int y = area.y();
    int rowHeight = 0;

    // The height of a row is known only when it is complete, so its items are placed then.
    const auto placeRow = [&] {
        if (!testOnly) {
            for (const Placed& placed : std::as_const(row)) {
                const int height = m_equalRowHeight ? rowHeight : placed.size.height();
                placed.item->setGeometry(QRect(placed.position, QSize(placed.size.width(), height)));
            }
        }
        row.clear();
    };

    for (QLayoutItem* item : m_items) {
        if (item->isEmpty())
            continue;
        const QSize size = item->sizeHint();
        // Start a new row when the item does not fit, unless it is the first of its row.
        if (x > area.x() && x + size.width() > area.right() + 1) {
            placeRow();
            x = area.x();
            y += rowHeight + m_verticalSpacing;
            rowHeight = 0;
        }
        row.append({item, QPoint(x, y), size});
        x += size.width() + m_horizontalSpacing;
        rowHeight = std::max(rowHeight, size.height());
    }
    placeRow();
    return y + rowHeight - rect.y() + margins.bottom();
}

} // namespace launcher::ui
