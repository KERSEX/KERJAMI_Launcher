#pragma once

#include "github/RegistryManager.h"
#include "models/Project.h"
#include "ui/Page.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace launcher::ui {

class ComboBox;
class ToggleSwitch;

/// Lists the approved projects from the registry and lets the user filter them.
///
/// The page only shows what the RegistryManager holds and asks it to refresh;
/// it does no network access itself.
class LibraryPage : public Page {
    Q_OBJECT

public:
    explicit LibraryPage(RegistryManager& registry, QWidget* parent = nullptr);

private:
    void onRegistryChanged();
    void rebuildList();
    void resetFilters();
    QList<Project> filteredProjects() const;
    QString statusText() const;

    RegistryManager& m_registry;
    QLineEdit* m_search = nullptr;
    ComboBox* m_category = nullptr;
    ComboBox* m_tag = nullptr;
    ToggleSwitch* m_featuredOnly = nullptr;
    QPushButton* m_refresh = nullptr;
    QLabel* m_status = nullptr;
    QWidget* m_listHolder = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
};

} // namespace launcher::ui
