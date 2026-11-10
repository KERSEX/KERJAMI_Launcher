#include "ui/Controls.h"

#include "ui/Theme.h"

#include <QPainter>
#include <QPen>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher::ui {

namespace {

constexpr int kNavHeight = 40;

constexpr int kTrackWidth = 40;
constexpr int kTrackHeight = 22;
constexpr int kSwitchMargin = 3; // room for the focus ring
constexpr qreal kKnobInset = 4.0;

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(static_cast<float>(alpha));
    return color;
}

/// Linear blend between two colours, t in [0, 1].
QColor mix(const QColor& from, const QColor& to, qreal t)
{
    const auto channel = [t](int a, int b) { return qRound(a + (b - a) * t); };
    return QColor(
        channel(from.red(), to.red()), channel(from.green(), to.green()), channel(from.blue(), to.blue()));
}

} // namespace

// ---- NavButton ---------------------------------------------------------------

NavButton::NavButton(char16_t glyph, const QString& text, QWidget* parent)
    : QAbstractButton(parent)
    , m_glyph(glyph)
{
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover); // repaint when the pointer enters or leaves
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize NavButton::sizeHint() const
{
    return {200, kNavHeight};
}

void NavButton::paintEvent(QPaintEvent*)
{
    const ThemePalette& colors = Theme::palette();
    const bool active = isChecked();
    const bool hovered = underMouse();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (active || hovered || hasFocus()) {
        if (hasFocus())
            painter.setPen(QPen(colors.accent, 1.0));
        else
            painter.setPen(Qt::NoPen);
        painter.setBrush(active ? withAlpha(colors.accent, 0.14) : colors.card);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);
    }

    if (active) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.accent);
        painter.drawRoundedRect(QRectF(0.0, (height() - 18) / 2.0, 3.0, 18.0), 1.5, 1.5);
    }

    const QColor textColor = (active || hovered) ? colors.textPrimary : colors.textSecondary;

    int textLeft = 16;
    if (Theme::hasIconFont()) {
        painter.setFont(Theme::iconFont(16));
        painter.setPen(active ? colors.accent : textColor);
        painter.drawText(QRect(14, 0, 22, height()), Qt::AlignCenter, QString(QChar(m_glyph)));
        textLeft = 48;
    }

    QFont textFont = font();
    textFont.setWeight(active ? QFont::DemiBold : QFont::Normal);
    painter.setFont(textFont);
    painter.setPen(textColor);
    painter.drawText(
        QRect(textLeft, 0, width() - textLeft - 8, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
}

// ---- ToggleSwitch ------------------------------------------------------------

ToggleSwitch::ToggleSwitch(QWidget* parent)
    : QAbstractButton(parent)
    , m_animation(new QPropertyAnimation(this, "position", this))
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_animation->setDuration(120);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);

    connect(this, &QAbstractButton::toggled, this, [this](bool checked) {
        const qreal target = checked ? 1.0 : 0.0;
        m_animation->stop();
        // A state applied while the page is being built must not animate.
        if (!isVisible()) {
            setPosition(target);
            return;
        }
        m_animation->setStartValue(m_position);
        m_animation->setEndValue(target);
        m_animation->start();
    });
}

QSize ToggleSwitch::sizeHint() const
{
    return {kTrackWidth + 2 * kSwitchMargin, kTrackHeight + 2 * kSwitchMargin};
}

void ToggleSwitch::setPosition(qreal position)
{
    m_position = std::clamp(position, 0.0, 1.0);
    update();
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    const ThemePalette& colors = Theme::palette();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled())
        painter.setOpacity(0.45);

    const QRectF track(
        (width() - kTrackWidth) / 2.0, (height() - kTrackHeight) / 2.0, kTrackWidth, kTrackHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(mix(colors.borderStrong, colors.accent, m_position));
    painter.drawRoundedRect(track, kTrackHeight / 2.0, kTrackHeight / 2.0);

    const qreal knob = kTrackHeight - 2 * kKnobInset;
    const qreal travel = kTrackWidth - knob - 2 * kKnobInset;
    painter.setBrush(mix(colors.textPrimary, colors.textOnAccent, m_position));
    painter.drawEllipse(
        QRectF(track.left() + kKnobInset + travel * m_position, track.top() + kKnobInset, knob, knob));

    if (hasFocus()) {
        const qreal radius = kTrackHeight / 2.0 + 2.0;
        painter.setPen(QPen(colors.accent, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(track.adjusted(-2.0, -2.0, 2.0, 2.0), radius, radius);
    }
}

// ---- ComboBox ----------------------------------------------------------------

ComboBox::ComboBox(QWidget* parent)
    : QComboBox(parent)
{
    // The default delegate ignores the "::item" rules of the style sheet.
    setItemDelegate(new QStyledItemDelegate(this));
    setCursor(Qt::PointingHandCursor);
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
}

void ComboBox::paintEvent(QPaintEvent* event)
{
    QComboBox::paintEvent(event);

    const ThemePalette& colors = Theme::palette();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QPen pen(isEnabled() ? colors.textSecondary : colors.borderStrong, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    const qreal x = width() - 17.0;
    const qreal y = height() / 2.0 + 0.5;
    painter.drawPolyline(
        QPolygonF({QPointF(x - 4.0, y - 2.0), QPointF(x, y + 2.0), QPointF(x + 4.0, y - 2.0)}));
}

void ComboBox::wheelEvent(QWheelEvent* event)
{
    event->ignore();
}

// ---- ElidedLabel -------------------------------------------------------------

ElidedLabel::ElidedLabel(const QString& text, QWidget* parent)
    : QLabel(text, parent)
{
    setToolTip(text);
}

QSize ElidedLabel::minimumSizeHint() const
{
    return {48, QLabel::minimumSizeHint().height()};
}

void ElidedLabel::paintEvent(QPaintEvent*)
{
    const QRect area = contentsRect();
    QPainter painter(this);
    painter.setPen(palette().color(foregroundRole()));
    painter.drawText(area, alignment().toInt() | Qt::TextSingleLine,
        fontMetrics().elidedText(text(), Qt::ElideMiddle, area.width()));
}

// ---- Card --------------------------------------------------------------------

Card::Card(QWidget* parent)
    : QFrame(parent)
    , m_body(new QVBoxLayout(this))
{
    setProperty("card", true);
    m_body->setContentsMargins(20, 18, 20, 18);
    m_body->setSpacing(12);
}

// ---- EmptyState --------------------------------------------------------------

EmptyState::EmptyState(char16_t glyph, const QString& title, const QString& text, QWidget* parent)
    : QWidget(parent)
    , m_layout(new QVBoxLayout(this))
{
    m_layout->setContentsMargins(24, 32, 24, 32);
    m_layout->setSpacing(6);

    if (Theme::hasIconFont()) {
        auto* icon = new QLabel(QString(QChar(glyph)), this);
        icon->setProperty("role", u"glyph"_s);
        icon->setFont(Theme::iconFont(30));
        icon->setAlignment(Qt::AlignCenter);
        m_layout->addWidget(icon);
        m_layout->addSpacing(8);
    }

    QLabel* titleLabel = createLabel(title, "title", this);
    titleLabel->setAlignment(Qt::AlignCenter);
    m_layout->addWidget(titleLabel);

    QLabel* textLabel = createLabel(text, "secondary", this);
    textLabel->setAlignment(Qt::AlignCenter);
    textLabel->setWordWrap(true);
    m_layout->addWidget(textLabel);
}

QPushButton* EmptyState::addAction(const QString& text)
{
    auto* button = new QPushButton(text, this);
    button->setProperty("accent", true);
    button->setCursor(Qt::PointingHandCursor);
    m_layout->addSpacing(12);
    m_layout->addWidget(button, 0, Qt::AlignHCenter);
    return button;
}

// ---- Helpers -----------------------------------------------------------------

QFrame* createDivider(QWidget* parent)
{
    auto* divider = new QFrame(parent);
    divider->setProperty("divider", true);
    divider->setFixedHeight(1);
    return divider;
}

QLabel* createLabel(const QString& text, const char* role, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", QString::fromLatin1(role));
    return label;
}

} // namespace launcher::ui
