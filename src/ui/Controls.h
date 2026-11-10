#pragma once

#include <QAbstractButton>
#include <QComboBox>
#include <QFrame>
#include <QLabel>

class QPropertyAnimation;
class QPushButton;
class QVBoxLayout;

/// Small reusable widgets shared by all pages.
namespace launcher::ui {

/// Entry in the sidebar: icon glyph, label and an accent marker when active.
class NavButton : public QAbstractButton {
    Q_OBJECT

public:
    NavButton(char16_t glyph, const QString& text, QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    char16_t m_glyph;
};

/// On/off switch with a short slide animation.
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal position READ position WRITE setPosition)

public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    QSize sizeHint() const override;

    /// Knob position from 0.0 (off) to 1.0 (on).
    qreal position() const { return m_position; }
    void setPosition(qreal position);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    qreal m_position = 0.0;
    QPropertyAnimation* m_animation;
};

/// Combo box with a themed arrow and popup list.
class ComboBox : public QComboBox {
    Q_OBJECT

public:
    explicit ComboBox(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

    /// The wheel scrolls the page instead of silently changing the setting
    /// under the cursor.
    void wheelEvent(QWheelEvent* event) override;
};

/// Single-line label that shortens long text in the middle and shows the full
/// text as tooltip. Used for file system paths.
class ElidedLabel : public QLabel {
    Q_OBJECT

public:
    explicit ElidedLabel(const QString& text, QWidget* parent = nullptr);

    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

/// Rounded container that groups related content.
class Card : public QFrame {
    Q_OBJECT

public:
    explicit Card(QWidget* parent = nullptr);

    /// Vertical layout for the card's content.
    QVBoxLayout* body() const { return m_body; }

private:
    QVBoxLayout* m_body;
};

/// Centered icon, headline and explanation for a page or section without content.
class EmptyState : public QWidget {
    Q_OBJECT

public:
    EmptyState(char16_t glyph, const QString& title, const QString& text, QWidget* parent = nullptr);

    /// Adds a button below the text and returns it.
    QPushButton* addAction(const QString& text);

private:
    QVBoxLayout* m_layout;
};

/// One-pixel horizontal line between rows of a card.
QFrame* createDivider(QWidget* parent = nullptr);

/// Label styled by its role: "hero", "section", "title", "body" or "secondary"
/// (see app.qss).
QLabel* createLabel(const QString& text, const char* role, QWidget* parent = nullptr);

} // namespace launcher::ui
