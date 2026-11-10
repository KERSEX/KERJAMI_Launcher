#pragma once

#include "ui/AppContext.h"
#include "ui/Page.h"

namespace launcher::ui {

/// Start page with the overview of installed projects and the launcher itself.
class DashboardPage : public Page {
    Q_OBJECT

public:
    explicit DashboardPage(const AppContext& context, QWidget* parent = nullptr);

signals:
    /// The user wants to browse the available projects.
    void libraryRequested();
};

} // namespace launcher::ui
