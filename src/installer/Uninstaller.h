#pragma once

#include "installer/InstalledProjects.h"
#include "installer/LibraryLayout.h"

#include <QHash>
#include <QObject>

#include <functional>

class QThread;

namespace launcher {

enum class UninstallError {
    None,
    Busy,               ///< The project is being installed or updated.
    NotInstalled,       ///< The project is not in the list of installed projects.
    ProtectedPath,      ///< The record points outside the library; nothing is touched.
    ProjectRunning,     ///< The project is running, or its files are in use.
    RegistrationFailed, ///< The list could not be saved; the project is back where it was.
};

struct UninstallResult {
    UninstallError error = UninstallError::None;
    QString detail; ///< Technical detail for the log.

    bool ok() const { return error == UninstallError::None; }
};

/// Removes an installed project.
///
/// Only the project's own folder (library/<install directory>) is ever
/// removed, and only if it lies directly inside the library and is no link.
/// The folder name comes from the launcher's own record, validated again here.
///
/// The folder is first moved to temp/. That is a rename, so it either happens
/// completely or fails at once if a file inside is in use; after it the
/// project is gone for the user. Deleting the files then runs in the
/// background. What a failed or interrupted deletion leaves in temp/ is
/// removed at the next start (LibraryLayout::cleanupTemp).
class Uninstaller : public QObject {
    Q_OBJECT

public:
    Uninstaller(const LibraryLayout& layout, InstalledProjects& installed, QObject* parent = nullptr);
    ~Uninstaller() override;

    /// Answers whether a project is running; it is then not removed.
    void setRunningCheck(std::function<bool(const QString& projectId)> check)
    {
        m_isRunning = std::move(check);
    }

    /// The caller has already asked the user for confirmation.
    UninstallResult uninstall(const QString& projectId);

signals:
    /// The project is gone from the library and from the list. Listeners drop
    /// whatever else they keep for the project, such as its update settings.
    void uninstalled(const QString& projectId);

    /// The background deletion ended; @p complete is false if files remain in temp/.
    void cleanupFinished(const QString& projectId, bool complete);

private:
    LibraryLayout m_layout;
    InstalledProjects& m_installed;
    std::function<bool(const QString&)> m_isRunning;
    QList<QThread*> m_cleanups;
};

} // namespace launcher
