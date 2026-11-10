#pragma once

#include <QLayout>
#include <QList>

namespace launcher::ui {

/// Places widgets side by side and wraps to a new row when the width runs out.
/// Used for tag chips, whose number and width are not known in advance.
class FlowLayout : public QLayout {
    Q_OBJECT

public:
    explicit FlowLayout(QWidget* parent = nullptr, int horizontalSpacing = 6, int verticalSpacing = 6);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;

    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override;

private:
    /// Lays the items out in @p rect and returns the height used.
    int doLayout(const QRect& rect, bool testOnly) const;

    QList<QLayoutItem*> m_items;
    int m_horizontalSpacing;
    int m_verticalSpacing;
};

} // namespace launcher::ui
