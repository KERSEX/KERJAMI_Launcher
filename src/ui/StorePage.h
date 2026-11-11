#pragma once

#include "github/RegistryManager.h"
#include "models/Project.h"
#include "ui/AppContext.h"
#include "ui/Page.h"

class QButtonGroup;
class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace launcher::ui {

class FlowLayout;

/// The store: everything that can be installed, as tiles in a grid, with a search and a
/// choice of category. This is where a project is found and installed; what is installed
/// is managed in the library. The page only shows what the RegistryManager holds.
class StorePage : public Page {
    Q_OBJECT

public:
    explicit StorePage(const AppContext& context, QWidget* parent = nullptr);

signals:
    /// The user wants the detail page of a project.
    void projectDetailsRequested(const QString& projectId);

private:
    void onRegistryChanged();
    void rebuildCategories();
    void rebuildTiles();
    void resetFilters();
    QList<Project> filteredProjects() const;
    QString chosenCategory() const;

    AppContext m_context;
    RegistryManager& m_registry;
    QLineEdit* m_search = nullptr;
    QWidget* m_categoryHolder = nullptr;
    FlowLayout* m_categoryLayout = nullptr;
    QButtonGroup* m_categories = nullptr;
    QLabel* m_status = nullptr;
    QWidget* m_messageHolder = nullptr;
    QVBoxLayout* m_messageLayout = nullptr;
    QWidget* m_tileHolder = nullptr;
    FlowLayout* m_tileLayout = nullptr;
};

} // namespace launcher::ui
