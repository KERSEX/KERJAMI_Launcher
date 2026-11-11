#pragma once

#include "models/Project.h"
#include "ui/AppContext.h"
#include "ui/Controls.h"

class QMouseEvent;

namespace launcher::ui {

/// One project in the store: name, category, a short description and the buttons to
/// install it (ProjectControls in the store mode).
///
/// All tiles have the same width and, as long as nothing is going on, the same height,
/// so that they line up in a grid. A click anywhere on the tile that is not a button
/// opens the detail page.
class StoreTile : public Card {
    Q_OBJECT

public:
    StoreTile(const Project& project, const AppContext& context, QWidget* parent = nullptr);

    QString projectId() const { return m_project.id; }

    QSize sizeHint() const override;

    /// The description as the tile shows it: cut at a word boundary, with an ellipsis.
    static QString shortDescription(const QString& text);

signals:
    /// The user wants the detail page of this project.
    void detailsRequested(const QString& projectId);

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    Project m_project;
};

} // namespace launcher::ui
