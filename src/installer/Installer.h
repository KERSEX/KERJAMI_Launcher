#pragma once

#include "installer/InstalledProjects.h"
#include "installer/LibraryLayout.h"
#include "models/Project.h"
#include "security/ArchiveValidator.h"

#include <QHash>
#include <QObject>

#include <atomic>
#include <functional>
#include <memory>

class QThread;

namespace launcher {

enum class InstallError {
    None,
    Busy,              ///< An installation of this project is already running.
    InvalidProject,    ///< Project fields or version fail the checks.
    NoTrustedHash,     ///< No SHA-256 to verify against. Such a file is never installed.
    HashMismatch,      ///< The file is not the one the release announced.
    ArchiveUnreadable, ///< Not a ZIP file, damaged, or a ZIP feature the launcher does not support.
    ArchiveRejected,   ///< The archive breaks a safety rule, see InstallResult::validation.
    InsufficientSpace,
    PathTooLong,        ///< The launcher folder lies so deep that Windows paths would exceed their limit.
    ExtractionFailed,   ///< Writing the files failed (disk, permissions, damaged data).
    ProjectRunning,     ///< The project is running, or its files are in use.
    SwapFailed,         ///< Replacing the installed version failed; the old version is back.
    RegistrationFailed, ///< The list of installed projects could not be saved; the old version is back.
    RollbackFailed,     ///< Going back failed too; files may be in a mixed state, see the detail.
    Cancelled,
};

struct InstallRequest {
    Project project; ///< Checked again by the installer.
    QString version; ///< Semantic version of the release.
    QString tag;
    QString archivePath; ///< The downloaded ZIP.
    QString sha256;      ///< Expected hash of the archive. Required.
};

struct InstallResult {
    InstallError error = InstallError::None;
    QString detail; ///< Technical detail for the log.
    ArchiveValidator::Problem validationProblem = ArchiveValidator::Problem::None;
    QString backupDirectory; ///< Where the previous version was kept, empty if there was none.

    bool ok() const { return error == InstallError::None; }
};

/// Installs a downloaded release into the library, all or nothing.
///
/// The order is fixed and every step has to succeed before the next one starts:
///   1. check the project and that it is not running,
///   2. verify the archive's SHA-256,
///   3. open the archive and validate every entry (ArchiveValidator),
///   4. extract into temp/staging-<id>/ and check the start file,
///   5. move the installed version to backups/<version>/,
///   6. move the staging folder to current/,
///   7. register the new version.
/// A failure up to step 4 touches nothing in the library. A failure in step 5
/// to 7 moves the previous version back. The work runs in a background thread.
class Installer : public QObject {
    Q_OBJECT

public:
    enum class Stage {
        Verifying,
        Extracting,
        Replacing,
    };
    Q_ENUM(Stage)

    /// Backups of earlier versions that are kept per project.
    static constexpr int kKeptBackups = 2;

    /// Free space that must remain on top of the extracted files.
    static constexpr qint64 kSpaceReserve = 16LL * 1024 * 1024;

    Installer(const LibraryLayout& layout, InstalledProjects& installed, QObject* parent = nullptr);
    ~Installer() override;

    /// Answers whether a project is running; its installation is then refused.
    /// Set by the process manager; by default nothing counts as running. Called
    /// from the worker thread, so it must be safe to call from any thread.
    void setRunningCheck(std::function<bool(const QString& projectId)> check)
    {
        m_isRunning = std::move(check);
    }

    /// How the free space of a folder's drive is determined, in bytes (-1 if
    /// unknown, which skips the check). Replaced only by tests.
    void setAvailableSpaceFunction(std::function<qint64(const QString&)> function)
    {
        m_availableSpace = std::move(function);
    }

    /// Limits for archives. The defaults suit production; tests narrow them.
    void setArchiveLimits(const ArchiveValidator::Limits& limits) { m_limits = limits; }

    bool isBusy(const QString& projectId) const { return m_jobs.contains(projectId); }

    /// Starts an installation. The outcome arrives through installed() or failed().
    /// A request for a project that is already being installed fails with Busy.
    void install(const InstallRequest& request);

    /// Stops an installation that has not replaced the installed version yet.
    void cancel(const QString& projectId);

signals:
    /// Bytes processed and total of the current stage.
    void progress(const QString& projectId, launcher::Installer::Stage stage, qint64 done, qint64 total);

    void installed(const QString& projectId, const launcher::InstallResult& result);
    void failed(const QString& projectId, const launcher::InstallResult& result);

private:
    struct Job;
    InstallResult run(Job& job);
    void onFinished(const QString& projectId);

    LibraryLayout m_layout;
    InstalledProjects& m_installed;
    std::function<bool(const QString&)> m_isRunning;
    std::function<qint64(const QString&)> m_availableSpace;
    ArchiveValidator::Limits m_limits;
    QHash<QString, std::shared_ptr<Job>> m_jobs;
};

} // namespace launcher

Q_DECLARE_METATYPE(launcher::InstallResult)
