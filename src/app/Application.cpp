#include "app/Application.h"

#include "app/Branding.h"
#include "core/Logger.h"
#include "core/PathManager.h"
#include "core/SettingsManager.h"
#include "downloads/DownloadManager.h"
#include "github/GitHubClient.h"
#include "github/RegistryManager.h"
#include "github/RegistryParser.h"
#include "github/ReleaseManager.h"
#include "installer/FileOps.h"
#include "installer/InstallService.h"
#include "installer/InstalledProjects.h"
#include "installer/Installer.h"
#include "installer/LibraryLayout.h"
#include "installer/Uninstaller.h"
#include "process/ProcessManager.h"
#include "ui/AppContext.h"
#include "ui/MainWindow.h"
#include "ui/OperationMessages.h"
#include "ui/Theme.h"
#include "updater/LauncherUpdateManager.h"
#include "updater/ProjectUpdateManager.h"
#include "updater/UpdateStartup.h"

#include <QDir>
#include <QLockFile>
#include <QMessageBox>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

/// Two launchers working on the same portable folder would corrupt downloads
/// and installations, so the folder is locked for the lifetime of the process.
bool acquireInstanceLock(QLockFile& lock)
{
    // The lock is held for hours; it must never expire by age.
    lock.setStaleLockTime(0);
    if (lock.tryLock(0))
        return true;

    // A lock file left behind by a crash on another computer (folder on a USB
    // drive) cannot be judged by its process id. Windows keeps the file of a
    // running instance open, so removing it only succeeds for such leftovers.
    return lock.error() == QLockFile::LockFailedError && lock.removeStaleLockFile() && lock.tryLock(0);
}

} // namespace

Application::Application(int& argc, char** argv)
    : m_app(argc, argv)
{
    QCoreApplication::setApplicationName(branding::productName());
    QCoreApplication::setApplicationVersion(branding::version());
    QGuiApplication::setApplicationDisplayName(branding::displayName());
}

int Application::run()
{
    // Applied first so that the error dialogs below already use the theme.
    // Version 1 has a single theme; the stored preference is not consulted yet.
    ui::Theme::apply(m_app, ThemeId::Dark);

    const PathManager paths = PathManager::forApplication();
    const PathManager::PrepareResult prepared = paths.prepare();
    if (!prepared.ok()) {
        const QString folder = QDir::toNativeSeparators(prepared.path);
        const QString product = branding::productName();
        const QString hint = tr("%1 ist eine portable Anwendung und speichert alle Daten im eigenen "
                                "Programmordner. Verschiebe den gesamten Ordner an einen Ort mit "
                                "Schreibrechten, zum Beispiel in deine Dokumente, und starte %1 erneut.")
                                 .arg(product);

        QString message;
        switch (prepared.error) {
        case PathManager::PrepareError::RootMissing:
            message = tr("Der Programmordner wurde nicht gefunden:\n%1").arg(folder);
            break;
        case PathManager::PrepareError::CannotCreateDirectory:
            message = tr("Dieser Ordner konnte nicht angelegt werden:\n%1\n\n%2").arg(folder, hint);
            break;
        case PathManager::PrepareError::NotWritable:
            message =
                tr("%1 kann in diesem Ordner keine Dateien speichern:\n%2\n\n%3").arg(product, folder, hint);
            break;
        case PathManager::PrepareError::None:
            break;
        }
        QMessageBox::critical(nullptr, tr("Start nicht möglich"), message);
        return ExitStorageUnavailable;
    }

    // The lock comes before the logger: the log file supports one writing
    // process only, and a second instance must not touch it.
    QLockFile instanceLock(paths.lockFile());
    if (!acquireInstanceLock(instanceLock)) {
        QMessageBox::information(nullptr, branding::displayName(),
            tr("%1 läuft bereits aus diesem Ordner.").arg(branding::productName()));
        return ExitOk;
    }

    Logger::Options logOptions;
    logOptions.directory = paths.logsDir();
    logOptions.redactRoot = paths.root();
    Logger::install(logOptions);
    qCInfo(lcApp).noquote() << branding::displayName() << branding::version() << "starting";

    // An update of the launcher may still be working on its files (the updater
    // starts the new launcher a moment before it finishes), or an earlier one
    // was interrupted. Both are dealt with before anything reads the files.
    const StartupUpdateOutcome startup = UpdateStartup::run(paths);
    if (startup.updaterStillRunning) {
        QMessageBox::information(nullptr, branding::displayName(),
            tr("Ein Update von %1 läuft noch. Starte %1 in einer Minute erneut.")
                .arg(branding::productName()));
        return ExitOk;
    }

    SettingsManager settings(paths.settingsFile());
    const SettingsManager::LoadResult loadResult = settings.load();

    // The client is created before the registry and destroyed after it, so a
    // request that is still running at shutdown is cancelled while its client exists.
    GitHubClient client(GitHubClient::Policy::production());
    RegistryManager registry(client, paths.registryCacheFile(), branding::registryUrl());
    registry.loadCache();

    const auto applyRegistrySettings = [&settings, &registry] {
        const LauncherSettings& current = settings.settings();
        registry.setAutoRefresh(current.registryAutoCheck, current.updateCheckIntervalMinutes);
    };
    applyRegistrySettings();
    QObject::connect(&settings, &SettingsManager::settingsChanged, &registry, applyRegistrySettings);

    // Created after the client and destroyed before it: running transfers are
    // stopped and their partial files removed while the client still exists.
    DownloadManager downloads(client, paths.tempDir(), paths.downloadsDir());

    // What an interrupted installation or removal left in temp/ is of no use.
    // Nothing else is running yet: the instance lock is held.
    const LibraryLayout layout(paths.libraryDir(), paths.tempDir());
    if (const int removed = layout.cleanupTemp(); removed > 0)
        qCInfo(lcApp) << "Removed" << removed << "leftover folder(s) from temp";

    // Archives an earlier run left in downloads/ (a crash, or an update that
    // was waiting for a project to close) are not picked up again: the next
    // update downloads and verifies them anew. Only folders named like a
    // project are touched.
    const QDir downloadFolders(paths.downloadsDir());
    for (const QString& name : downloadFolders.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (RegistryParser::isValidProjectId(name))
            FileOps::removeTree(downloadFolders.filePath(name));
    }

    InstalledProjects installed(paths.stateFile());
    const InstalledProjects::LoadResult installedResult = installed.load();

    // Destroyed in reverse order: the service first, then the workers it uses.
    ReleaseManager releases(client);
    Installer installer(layout, installed);
    Uninstaller uninstaller(layout, installed);
    InstallService installService(client, releases, downloads, installer, uninstaller, installed, layout);

    // A project counts as running while a program of it is alive. An update, a
    // removal or a second start must not touch such a project.
    ProcessManager processes(layout, installed);
    const auto isRunning = [&processes](const QString& id) { return processes.isRunningNow(id); };
    installer.setRunningCheck(isRunning);
    uninstaller.setRunningCheck(isRunning);
    installService.setStarter([&processes](const QString& id) { return processes.start(id); });

    ProjectUpdateManager updates(client, registry, releases, installService, processes, installed, settings);

    // The launcher's own update. It asks for the user's confirmation and, once
    // the new version is prepared, hands over to the updater and closes the
    // launcher; it does not start while projects are being installed.
    LauncherUpdateManager launcherUpdates(client, releases, downloads, settings, paths);
    launcherUpdates.setBusyCheck([&installService] { return installService.busyCount() > 0; });
    QObject::connect(
        &launcherUpdates, &LauncherUpdateManager::restartRequested, &m_app, &QCoreApplication::quit);

    ui::AppContext context {
        paths, settings, registry, downloads, installed, installService, processes, updates, launcherUpdates};
    ui::MainWindow window(context);
    window.show();
    updates.start();
    launcherUpdates.start();

    // How the last update ended, and what was done about an interrupted one.
    const auto showStartupMessage = [&window](const std::optional<ui::StartupMessage>& message) {
        if (!message)
            return;
        switch (message->level) {
        case ui::StartupMessage::Level::Information:
            QMessageBox::information(&window, message->title, message->text);
            break;
        case ui::StartupMessage::Level::Warning:
            QMessageBox::warning(&window, message->title, message->text);
            break;
        case ui::StartupMessage::Level::Critical:
            QMessageBox::critical(&window, message->title, message->text);
            break;
        }
    };
    if (startup.recovery)
        showStartupMessage(ui::recoveryMessage(*startup.recovery));
    if (startup.report)
        showStartupMessage(ui::updateReportMessage(*startup.report));

    if (settings.settings().registryAutoCheck)
        registry.refresh();

    if (loadResult == SettingsManager::LoadResult::RecoveredFromCorruption
        || loadResult == SettingsManager::LoadResult::RecoveredFromNewerSchema) {
        const QString reason = loadResult == SettingsManager::LoadResult::RecoveredFromCorruption
            ? tr("Die Einstellungsdatei war beschädigt.")
            : tr("Die Einstellungsdatei stammt von einer neueren Version.");
        QString message = reason + u' ' + tr("Es gelten wieder die Standardeinstellungen.");
        if (!settings.backupPath().isEmpty()) {
            message += u"\n\n"_s
                + tr("Die bisherige Datei wurde gesichert:\n%1")
                      .arg(QDir::toNativeSeparators(settings.backupPath()));
        }
        QMessageBox::warning(&window, tr("Einstellungen zurückgesetzt"), message);
    }

    if (installedResult == InstalledProjects::LoadResult::RecoveredFromCorruption
        || installedResult == InstalledProjects::LoadResult::RecoveredFromNewerSchema) {
        const QString reason = installedResult == InstalledProjects::LoadResult::RecoveredFromCorruption
            ? tr("Die Liste der installierten Projekte war beschädigt.")
            : tr("Die Liste der installierten Projekte stammt von einer neueren Version.");
        QString message = reason + u' '
            + tr("Die Projekte sind deshalb nicht als installiert eingetragen. Ihre Dateien liegen "
                 "unverändert im Ordner library und werden beim erneuten Installieren gesichert.");
        if (!installed.backupPath().isEmpty()) {
            message += u"\n\n"_s
                + tr("Die bisherige Datei wurde gesichert:\n%1")
                      .arg(QDir::toNativeSeparators(installed.backupPath()));
        }
        QMessageBox::warning(&window, tr("Installierte Projekte"), message);
    }

    const int exitCode = m_app.exec();
    qCInfo(lcApp) << "Shutting down with exit code" << exitCode;
    return exitCode;
}

} // namespace launcher
