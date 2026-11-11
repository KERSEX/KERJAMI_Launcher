#pragma once

#include "github/RegistryManager.h"
#include "models/Project.h"
#include "ui/AppContext.h"
#include "ui/Page.h"

#include <QTimer>

class QLabel;
class QVBoxLayout;

namespace launcher::ui {

/// What is installed: one short card per project, with its version and the buttons to
/// start and update it. Finding and installing projects happens in the store; removing a
/// project and choosing how it is updated happen on its detail page.
///
/// The list follows the managers and is built again (once, a moment after the last change)
/// when a project is installed or removed or when the list of projects changes.
class LibraryPage : public Page {
    Q_OBJECT

public:
    explicit LibraryPage(const AppContext& context, QWidget* parent = nullptr);

signals:
    /// The user wants the detail page of a project.
    void projectDetailsRequested(const QString& projectId);

    /// The user wants to look for projects to install.
    void storeRequested();

private:
    void rebuildList();

    /// The project an installation stands for: the entry of the list, or, for a project
    /// that has left the list, one made from what the installation recorded.
    Project projectFor(const QString& projectId, bool* listed) const;

    AppContext m_context;
    QTimer m_rebuild;
    bool m_built = false;
    QStringList m_signature; ///< What the list shows now; see rebuildList().
    QLabel* m_status = nullptr;
    QWidget* m_listHolder = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
};

} // namespace launcher::ui
