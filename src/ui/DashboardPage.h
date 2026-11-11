#pragma once

#include "ui/AppContext.h"
#include "ui/Page.h"

#include <QTimer>

class QLabel;
class QVBoxLayout;

namespace launcher::ui {

class Card;

/// The start page: a welcome with the way to the store, the updates that are
/// available, the projects at hand (started last first), and the launcher itself
/// with its update notice. Everything that can be installed is in the store and
/// everything that is installed is in the library; this page only shows what needs
/// attention or is wanted right now.
///
/// The lists follow the managers: they are built again (once, a moment after the last
/// change) when a project is installed, removed, started or updated, or when the list
/// of projects changes.
class DashboardPage : public Page {
    Q_OBJECT

public:
    explicit DashboardPage(const AppContext& context, QWidget* parent = nullptr);

signals:
    /// The user wants to look for projects to install.
    void storeRequested();

    /// The user wants the detail page of a project.
    void projectRequested(const QString& projectId);

private:
    struct Section {
        QLabel* title = nullptr;
        Card* card = nullptr;
        QVBoxLayout* body = nullptr;
    };

    Section createSection(const QString& title, const char* objectName);
    void rebuildLists();
    void scheduleRebuild() { m_rebuild.start(); }

    /// One project in a list: name and detail line, "Starten" if @p withStart, "Details".
    QWidget* createProjectRow(
        const QString& projectId, const QString& name, const QString& detail, bool withStart);
    void fill(Section& section, const QList<QWidget*>& rows, const QString& emptyText);

    AppContext m_context;
    QTimer m_rebuild;
    Section m_updates;
    Section m_recent;
};

} // namespace launcher::ui
