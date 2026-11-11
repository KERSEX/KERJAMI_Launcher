#pragma once

#include "ui/AppContext.h"

#include <QMainWindow>

class QButtonGroup;
class QStackedWidget;

namespace launcher::ui {

class ProjectDetailPage;

/// Main window: navigation sidebar on the left, the selected page on the right.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /// Pages in sidebar order. The values are indices into the page stack.
    enum class PageId {
        Dashboard,
        Library,
        Downloads,
        Settings,
        ProjectDetail, ///< Not in the sidebar: opened from the library or the dashboard.
    };

    explicit MainWindow(const AppContext& context, QWidget* parent = nullptr);

    PageId currentPage() const;
    void showPage(PageId page);

    /// Opens the detail page of a project of the list; the page it was opened from is
    /// where "Zurück" leads. Does nothing for an id that is not in the list.
    void showProject(const QString& projectId);

private:
    QWidget* createSidebar();
    void switchTo(PageId page, bool animate);
    void fadeIn(QWidget* page);

    AppContext m_context;
    QButtonGroup* m_navigation = nullptr;
    QStackedWidget* m_pages = nullptr;
    ProjectDetailPage* m_detail = nullptr;
    PageId m_detailOrigin = PageId::Library;
};

} // namespace launcher::ui
