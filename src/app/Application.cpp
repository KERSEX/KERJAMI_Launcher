#include "app/Application.h"

#include "app/Branding.h"
#include "core/Logger.h"
#include "core/PathManager.h"
#include "core/SettingsManager.h"
#include "github/GitHubClient.h"
#include "github/RegistryManager.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

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

    ui::MainWindow window(paths, settings, registry);
    window.show();

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

    const int exitCode = m_app.exec();
    qCInfo(lcApp) << "Shutting down with exit code" << exitCode;
    return exitCode;
}

} // namespace launcher
