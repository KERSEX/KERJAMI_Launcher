#pragma once

#include "github/RegistryManager.h"
#include "models/Project.h"
#include "ui/AppContext.h"
#include "ui/Page.h"

class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace launcher::ui {

class ComboBox;
class ToggleSwitch;

/// Lists the approved projects (the list built into the program) and lets the
/// user filter them. The page only shows what the RegistryManager holds.
class LibraryPage : public Page {
    Q_OBJECT

public:
    explicit LibraryPage(const AppContext& context, QWidget* parent = nullptr);

signals:
    /// The user wants the detail page of a project.
    void projectDetailsRequested(const QString& projectId);

private:
    void onRegistryChanged();
    void rebuildList();
    void resetFilters();
    QList<Project> filteredProjects() const;
    QString statusText() const;

    AppContext m_context;
    RegistryManager& m_registry;
    QLineEdit* m_search = nullptr;
    ComboBox* m_category = nullptr;
    ComboBox* m_tag = nullptr;
    ToggleSwitch* m_featuredOnly = nullptr;
    QLabel* m_status = nullptr;
    QWidget* m_listHolder = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
};

} // namespace launcher::ui
