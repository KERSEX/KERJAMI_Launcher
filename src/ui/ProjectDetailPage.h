#pragma once

#include "github/ReleaseManager.h"
#include "installer/InstallService.h"
#include "models/Project.h"
#include "ui/AppContext.h"
#include "ui/Page.h"

#include <QDateTime>
#include <QHash>

class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace launcher::ui {

/// Everything about one project: description, category, tags, the installed and the
/// newest version, the size of the download, the changelog, a link to GitHub, and
/// the buttons to install, start, update and remove it with the choice between manual
/// and automatic updates (ProjectControls).
///
/// The newest version comes from the release manager. Opening the page asks GitHub
/// once when nothing recent is known; the "Neu abfragen" button asks again.
class ProjectDetailPage : public Page {
    Q_OBJECT

public:
    explicit ProjectDetailPage(const AppContext& context, QWidget* parent = nullptr);

    /// Shows the project; builds the page anew. Asks for the newest release when needed.
    void showProject(const Project& project);

    /// The project shown, empty before the first showProject().
    QString projectId() const { return m_project.id; }

signals:
    /// The user wants to go back to where the page was opened from.
    void backRequested();

private:
    void rebuild();
    void clearContent();
    void refreshVersions();
    void requestRelease(bool force);
    void onReleaseReady(const QString& projectId);
    void onReleaseFailed(const QString& projectId, ReleaseManager::Problem problem, int httpStatus);

    AppContext m_context;
    Project m_project;
    bool m_hasProject = false;

    /// When the page last received a release of a project, so that opening it again soon does not ask again.
    QHash<QString, QDateTime> m_receivedAt;
    OperationFailure m_releaseFailure;

    QLabel* m_installedVersion = nullptr;
    QLabel* m_latestVersion = nullptr;
    QLabel* m_downloadSize = nullptr;
    QLabel* m_published = nullptr;
    QLabel* m_problem = nullptr;
    QPushButton* m_refresh = nullptr;
    QPlainTextEdit* m_notes = nullptr;
};

} // namespace launcher::ui
