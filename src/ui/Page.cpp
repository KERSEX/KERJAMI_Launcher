#include "ui/Page.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace launcher::ui {

namespace {

constexpr int kPageMargin = 36;
constexpr int kMaxContentWidth = 1040;

} // namespace

Page::Page(const QString& title, const QString& subtitle, QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* header = new QVBoxLayout;
    header->setContentsMargins(kPageMargin, 30, kPageMargin, 22);
    header->setSpacing(4);

    auto* titleLabel = new QLabel(title, this);
    titleLabel->setObjectName("PageTitle");
    header->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel(subtitle, this);
    subtitleLabel->setObjectName("PageSubtitle");
    subtitleLabel->setWordWrap(true);
    header->addWidget(subtitleLabel);

    outer->addLayout(header);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* holder = new QWidget;
    auto* holderLayout = new QHBoxLayout(holder);
    holderLayout->setContentsMargins(kPageMargin, 0, kPageMargin, kPageMargin);
    holderLayout->setSpacing(0);

    auto* column = new QWidget(holder);
    column->setMaximumWidth(kMaxContentWidth);
    m_content = new QVBoxLayout(column);
    m_content->setContentsMargins(0, 0, 0, 0);
    m_content->setSpacing(14);

    holderLayout->addWidget(column, 1);
    holderLayout->addStretch(0); // takes the width beyond the column's maximum

    scroll->setWidget(holder);
    outer->addWidget(scroll, 1);
}

} // namespace launcher::ui
