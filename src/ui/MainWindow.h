#pragma once

#include "ui/AppContext.h"

#include <QMainWindow>

class QButtonGroup;
class QStackedWidget;

namespace launcher::ui {

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
    };

    explicit MainWindow(const AppContext& context, QWidget* parent = nullptr);

    PageId currentPage() const;
    void showPage(PageId page);

private:
    QWidget* createSidebar();
    void switchTo(PageId page, bool animate);
    void fadeIn(QWidget* page);

    QButtonGroup* m_navigation = nullptr;
    QStackedWidget* m_pages = nullptr;
};

} // namespace launcher::ui
