#pragma once

#include <QWidget>

class QVBoxLayout;

namespace launcher::ui {

/// Common frame of every page: title, subtitle and a scrollable content column
/// with a maximum width, so lines stay readable in a maximised window.
class Page : public QWidget {
    Q_OBJECT

public:
    Page(const QString& title, const QString& subtitle, QWidget* parent = nullptr);

protected:
    /// Layout of the content column. Subclasses add their cards here.
    QVBoxLayout* contentLayout() const { return m_content; }

private:
    QVBoxLayout* m_content;
};

} // namespace launcher::ui
