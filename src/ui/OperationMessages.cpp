#include "ui/OperationMessages.h"

#include "app/Branding.h"
#include "core/Format.h"

#include <QCoreApplication>
#include <QDir>

using namespace Qt::StringLiterals;

namespace launcher::ui {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("OperationMessages", text);
}

QString releaseText(const OperationFailure& failure)
{
    switch (static_cast<ReleaseManager::Problem>(failure.code)) {
    case ReleaseManager::Problem::None:
        break;
    case ReleaseManager::Problem::InvalidProject:
        return tr("Die Projektangaben in der Registry sind ungültig.");
    case ReleaseManager::Problem::Unreachable:
        return tr("Keine Verbindung zu GitHub.");
    case ReleaseManager::Problem::RateLimited:
        return tr("GitHub nimmt vorübergehend keine weiteren Anfragen an. Versuche es später noch einmal.");
    case ReleaseManager::Problem::Http:
        if (failure.httpStatus == 404)
            return tr("Das Repository oder seine Releases wurden auf GitHub nicht gefunden (HTTP 404).");
        return tr("GitHub antwortete mit HTTP %1.").arg(failure.httpStatus);
    case ReleaseManager::Problem::InvalidContent:
        return tr("Die Release-Informationen von GitHub sind nicht lesbar.");
    case ReleaseManager::Problem::NoRelease:
        return tr("Für dieses Projekt gibt es noch kein veröffentlichtes Release.");
    case ReleaseManager::Problem::NoMatchingAsset:
        return tr("Das neueste Release enthält keine passende Datei zum Installieren.");
    case ReleaseManager::Problem::AmbiguousAsset:
        return tr("Das neueste Release enthält mehrere passende Dateien. Die Auswahl ist nicht eindeutig, "
                  "deshalb wird nichts installiert.");
    }
    return tr("Das Release konnte nicht ermittelt werden.");
}

QString checksumText(const OperationFailure& failure)
{
    if (failure.code == 0)
        return tr("Die Prüfsummendatei des Releases nennt keinen Hash für die Installationsdatei. Es wird "
                  "nichts installiert.");
    if (failure.code == static_cast<int>(NetworkError::HttpError) && failure.httpStatus > 0)
        return tr("Die Prüfsummendatei des Releases konnte nicht geladen werden (HTTP %1).")
            .arg(failure.httpStatus);
    return tr("Die Prüfsummendatei des Releases konnte nicht geladen werden.");
}

QString downloadText(const OperationFailure& failure)
{
    switch (static_cast<DownloadProblem>(failure.code)) {
    case DownloadProblem::None:
        break;
    case DownloadProblem::InvalidRequest:
        return tr("Der Download wurde aus Sicherheitsgründen abgelehnt.");
    case DownloadProblem::InsufficientSpace:
        return tr("Nicht genug freier Speicherplatz für den Download.");
    case DownloadProblem::StorageError:
        return tr("Die heruntergeladene Datei konnte nicht gespeichert werden.");
    case DownloadProblem::Unreachable:
        return tr("Keine Verbindung zu GitHub. Der Download wurde abgebrochen.");
    case DownloadProblem::RateLimited:
        return tr("GitHub nimmt vorübergehend keine weiteren Anfragen an. Versuche es später noch einmal.");
    case DownloadProblem::Http:
        return tr("Der Download schlug fehl (HTTP %1).").arg(failure.httpStatus);
    case DownloadProblem::SizeMismatch:
        return tr("Die heruntergeladene Datei hat nicht die angekündigte Größe.");
    case DownloadProblem::TooLarge:
        return tr("Der Server sendet mehr Daten als angekündigt. Der Download wurde abgebrochen.");
    }
    return tr("Der Download ist fehlgeschlagen.");
}

QString archiveRejectedText(int problem)
{
    switch (static_cast<ArchiveValidator::Problem>(problem)) {
    case ArchiveValidator::Problem::None:
        break;
    case ArchiveValidator::Problem::Empty:
        return tr("Das Archiv enthält keine Dateien.");
    case ArchiveValidator::Problem::TooManyEntries:
        return tr("Das Archiv enthält unplausibel viele Einträge.");
    case ArchiveValidator::Problem::TooLarge:
        return tr("Das Archiv ist entpackt größer als erlaubt.");
    case ArchiveValidator::Problem::UnsafePath:
        return tr("Das Archiv enthält Pfade, die aus dem Projektordner hinausführen oder unter Windows nicht "
                  "zulässig sind.");
    case ArchiveValidator::Problem::DuplicatePath:
        return tr("Das Archiv enthält Dateien, die unter Windows denselben Namen hätten.");
    case ArchiveValidator::Problem::ConflictingPath:
        return tr("Das Archiv verwendet einen Namen zugleich als Datei und als Ordner.");
    case ArchiveValidator::Problem::UnsupportedEntry:
        return tr("Das Archiv enthält Verknüpfungen oder Sonderdateien.");
    case ArchiveValidator::Problem::CompressionBomb:
        return tr("Das Archiv ist unplausibel stark komprimiert.");
    case ArchiveValidator::Problem::MissingExecutable:
        return tr("Das Archiv enthält die Startdatei des Projekts nicht.");
    }
    return tr("Das Archiv hat den Sicherheitsprüfungen nicht standgehalten.");
}

QString installText(const OperationFailure& failure)
{
    switch (static_cast<InstallError>(failure.code)) {
    case InstallError::None:
        break;
    case InstallError::Busy:
        return tr("Für dieses Projekt läuft bereits eine Installation.");
    case InstallError::InvalidProject:
        return tr("Die Projektangaben sind ungültig. Es wurde nichts verändert.");
    case InstallError::NoTrustedHash:
        return tr("Es gibt keine SHA-256-Prüfsumme für die Datei. Ohne Prüfsumme wird nichts installiert.");
    case InstallError::HashMismatch:
        return tr("Die Prüfsumme der heruntergeladenen Datei stimmt nicht mit dem Release überein. Die Datei "
                  "wurde verworfen, es wurde nichts installiert.");
    case InstallError::ArchiveUnreadable:
        return tr("Die heruntergeladene Datei ist kein lesbares ZIP-Archiv.");
    case InstallError::ArchiveRejected:
        return archiveRejectedText(failure.subcode) + u' ' + tr("Es wurde nichts installiert.");
    case InstallError::InsufficientSpace:
        return tr("Nicht genug freier Speicherplatz zum Entpacken.");
    case InstallError::PathTooLong:
        return tr("%1 liegt so tief verschachtelt, dass Windows-Pfade zu lang würden. Verschiebe den "
                  "Programmordner näher an die Laufwerkswurzel.")
            .arg(branding::productName());
    case InstallError::ExtractionFailed:
        return tr("Beim Entpacken ist ein Fehler aufgetreten. Die bisherige Version bleibt erhalten.");
    case InstallError::ProjectRunning:
        return tr("Das Projekt läuft oder seine Dateien sind in Benutzung. Beende es und versuche es erneut. "
                  "Die bisherige Version bleibt erhalten.");
    case InstallError::SwapFailed:
        return tr("Die neue Version konnte nicht aktiviert werden. Die bisherige Version bleibt erhalten.");
    case InstallError::RegistrationFailed:
        return tr("Die Installation konnte nicht eingetragen werden, weil die Datei launcher.json nicht "
                  "geschrieben werden kann. Die bisherige Version wurde wiederhergestellt.");
    case InstallError::RollbackFailed:
        return tr("Die bisherige Version konnte nicht wiederhergestellt werden. Der Ordner des Projekts kann "
                  "in einem gemischten Zustand sein, Einzelheiten stehen im Protokoll.");
    case InstallError::Cancelled:
        return tr("Die Installation wurde abgebrochen.");
    }
    return tr("Die Installation ist fehlgeschlagen.");
}

QString uninstallText(const OperationFailure& failure)
{
    switch (static_cast<UninstallError>(failure.code)) {
    case UninstallError::None:
        break;
    case UninstallError::Busy:
        return tr(
            "Das Projekt wird gerade installiert. Brich die Installation ab oder warte, bis sie fertig ist.");
    case UninstallError::NotInstalled:
        return tr("Das Projekt ist nicht installiert.");
    case UninstallError::ProtectedPath:
        return tr("Der Ordner des Projekts liegt nicht in der Bibliothek oder ist eine Verknüpfung. Aus "
                  "Sicherheitsgründen wurde nichts gelöscht.");
    case UninstallError::ProjectRunning:
        return tr(
            "Das Projekt läuft oder seine Dateien sind in Benutzung. Beende es und versuche es erneut.");
    case UninstallError::RegistrationFailed:
        return tr("Die Deinstallation konnte nicht eingetragen werden. Das Projekt wurde nicht entfernt.");
    }
    return tr("Die Deinstallation ist fehlgeschlagen.");
}

QString startText(const OperationFailure& failure)
{
    switch (static_cast<StartError>(failure.code)) {
    case StartError::None:
        break;
    case StartError::Busy:
        return tr("Das Projekt wird gerade installiert.");
    case StartError::NotInstalled:
        return tr("Das Projekt ist nicht installiert.");
    case StartError::AlreadyRunning:
        return tr("Das Projekt läuft bereits.");
    case StartError::FilesMissing:
        return tr("Die Startdatei des Projekts fehlt. Installiere das Projekt erneut.");
    case StartError::ProtectedPath:
        return tr("Die Startdatei liegt nicht im Ordner des Projekts oder eine Verknüpfung ist im Weg. Aus "
                  "Sicherheitsgründen wurde nichts gestartet.");
    case StartError::StartFailed:
        return tr("Windows konnte das Programm nicht starten.");
    }
    return tr("Das Programm konnte nicht gestartet werden.");
}

} // namespace

QString failureText(const OperationFailure& failure)
{
    switch (failure.kind) {
    case OperationFailure::Kind::None:
        return {};
    case OperationFailure::Kind::Release:
        return releaseText(failure);
    case OperationFailure::Kind::NoHash:
        return tr("Dieses Release enthält weder eine GitHub-Prüfsumme noch eine Prüfsummendatei. Ohne "
                  "Prüfsumme wird nichts heruntergeladen oder installiert.");
    case OperationFailure::Kind::Checksum:
        return checksumText(failure);
    case OperationFailure::Kind::Download:
        return downloadText(failure);
    case OperationFailure::Kind::Install:
        return installText(failure);
    case OperationFailure::Kind::Uninstall:
        return uninstallText(failure);
    case OperationFailure::Kind::Start:
        return startText(failure);
    }
    return {};
}

QString stageText(const ProjectStatus& status)
{
    const QString version = status.version.isEmpty() ? QString() : u' ' + status.version;
    switch (status.stage) {
    case ProjectStatus::Stage::Idle:
        return {};
    case ProjectStatus::Stage::FetchingRelease:
        return tr("Neuestes Release wird gesucht …");
    case ProjectStatus::Stage::FetchingChecksum:
        return tr("Prüfsumme wird gelesen …");
    case ProjectStatus::Stage::Downloading:
        if (status.total > 0)
            return tr("Version%1 wird geladen: %2 von %3")
                .arg(version, format::bytes(status.done), format::bytes(status.total));
        return tr("Version%1 wird geladen …").arg(version);
    case ProjectStatus::Stage::Verifying:
        return tr("Prüfsumme wird geprüft …");
    case ProjectStatus::Stage::Extracting:
        return tr("Wird entpackt …");
    case ProjectStatus::Stage::Replacing:
        return tr("Wird installiert …");
    case ProjectStatus::Stage::Prepared:
        return tr("Update ist bereit und wartet auf das Ende des Projekts.");
    }
    return {};
}

QString launcherUpdateFailureText(const LauncherUpdateFailure& failure)
{
    // The texts of the project chain are reused: the steps and their problems are the same.
    OperationFailure shared;
    shared.code = failure.code;
    shared.subcode = failure.subcode;
    shared.httpStatus = failure.httpStatus;

    switch (failure.kind) {
    case LauncherUpdateFailure::Kind::None:
    case LauncherUpdateFailure::Kind::Busy:
        return {};
    case LauncherUpdateFailure::Kind::Release:
        return releaseText(shared);
    case LauncherUpdateFailure::Kind::NoHash:
        return tr("Dieses Release enthält weder eine GitHub-Prüfsumme noch eine Prüfsummendatei. Ohne "
                  "Prüfsumme wird nichts heruntergeladen oder installiert.");
    case LauncherUpdateFailure::Kind::Checksum:
        return checksumText(shared);
    case LauncherUpdateFailure::Kind::Download:
        return downloadText(shared);
    case LauncherUpdateFailure::Kind::Package:
        switch (static_cast<PackageError>(failure.code)) {
        case PackageError::None:
            break;
        case PackageError::NoTrustedHash:
            return tr("Es gibt keine SHA-256-Prüfsumme für das Update. Es wurde nichts verändert.");
        case PackageError::HashMismatch:
            return tr("Die Prüfsumme der heruntergeladenen Datei stimmt nicht mit dem Release überein. Die "
                      "Datei wurde verworfen, es wurde nichts verändert.");
        case PackageError::ArchiveUnreadable:
            return tr("Die heruntergeladene Datei ist kein lesbares ZIP-Archiv. Es wurde nichts verändert.");
        case PackageError::ArchiveRejected:
            return archiveRejectedText(failure.subcode) + u' ' + tr("Es wurde nichts verändert.");
        case PackageError::PathTooLong:
            return tr("%1 liegt so tief verschachtelt, dass Windows-Pfade zu lang würden. Verschiebe den "
                      "Programmordner näher an die Laufwerkswurzel.")
                .arg(branding::productName());
        case PackageError::InsufficientSpace:
            return tr("Nicht genug freier Speicherplatz, um das Update vorzubereiten.");
        case PackageError::ExtractionFailed:
            return tr("Beim Entpacken des Updates ist ein Fehler aufgetreten. Es wurde nichts verändert.");
        case PackageError::ManifestMissing:
            return tr(
                "Das Update enthält keine Dateiliste (update-manifest.json). Es wurde nichts verändert.");
        case PackageError::ManifestInvalid:
            return tr("Die Dateiliste des Updates ist ungültig oder verlangt Zugriff auf Ordner, die ein "
                      "Update nicht anfassen darf. Es wurde nichts verändert.");
        case PackageError::VersionMismatch:
            return tr("Die Dateiliste des Updates gehört zu einer anderen Version als das Release. Es wurde "
                      "nichts verändert.");
        case PackageError::FilesMismatch:
            return tr(
                "Eine Datei im Update stimmt nicht mit der Dateiliste überein. Es wurde nichts verändert.");
        case PackageError::Cancelled:
            return tr("Das Update wurde abgebrochen.");
        }
        return tr("Das Update konnte nicht vorbereitet werden.");
    case LauncherUpdateFailure::Kind::Handoff:
        if (failure.code == 1)
            return tr("%1 muss unter dem Namen %2 laufen, um sich selbst zu aktualisieren.")
                .arg(branding::productName(), branding::executableFileName());
        if (failure.code == 3)
            return tr("Das Update-Programm konnte nicht gestartet werden. Es wurde nichts verändert.");
        return tr(
            "Das Update konnte nicht an das Update-Programm übergeben werden. Es wurde nichts verändert.");
    }
    return tr("Das Update ist fehlgeschlagen.");
}

QString launcherUpdateStatusText(const LauncherUpdateInfo& info)
{
    const QString product = branding::productName();
    switch (info.state) {
    case LauncherUpdateState::Idle: {
        QString text =
            tr("Noch nicht nach Updates gesucht (installiert: Version %1).").arg(info.currentVersion);
        if (info.lastCheckFailed)
            text += u' ' + tr("Die letzte Suche war nicht möglich.");
        return text;
    }
    case LauncherUpdateState::Checking:
        return tr("Suche nach Updates …");
    case LauncherUpdateState::UpToDate:
        return tr("%1 ist aktuell (Version %2).").arg(product, info.currentVersion)
            + (info.lastCheckFailed ? u' ' + tr("Die letzte Suche war nicht möglich.") : QString());
    case LauncherUpdateState::Available:
        return tr("Version %1 ist verfügbar (installiert: Version %2).")
                   .arg(info.latestVersion, info.currentVersion)
            + (info.lastCheckFailed ? u' ' + tr("Die letzte Suche war nicht möglich.") : QString());
    case LauncherUpdateState::Downloading:
        if (info.total > 0)
            return tr("Version %1 wird geladen: %2 von %3")
                .arg(info.latestVersion, format::bytes(info.done), format::bytes(info.total));
        return tr("Version %1 wird geladen …").arg(info.latestVersion);
    case LauncherUpdateState::Preparing:
        return tr("Das Update wird geprüft und vorbereitet …");
    case LauncherUpdateState::Ready: {
        QString text = tr("Das Update auf Version %1 ist bereit.").arg(info.latestVersion);
        if (info.failure.kind == LauncherUpdateFailure::Kind::Busy)
            text += u' ' + tr("Es wartet, bis die Installationen von Projekten beendet sind.");
        return text;
    }
    case LauncherUpdateState::Applying:
        return tr("%1 wird jetzt beendet, aktualisiert und startet danach neu.").arg(product);
    case LauncherUpdateState::Failed:
        return info.updateKnown()
            ? tr("Das Update auf Version %1 ist fehlgeschlagen.").arg(info.latestVersion)
            : tr("Die Suche nach Updates ist fehlgeschlagen.");
    }
    return {};
}

std::optional<StartupMessage> updateReportMessage(const UpdateReport& report)
{
    StartupMessage message;
    const QString product = branding::productName();
    switch (report.status) {
    case UpdateReport::Status::Success:
        message.title = tr("Aktualisiert");
        message.text = tr("%1 wurde auf Version %2 aktualisiert.").arg(product, report.toVersion);
        if (!report.backupDir.isEmpty())
            message.text += u"\n\n"_s
                + tr("Die ersetzten Programmdateien liegen als Sicherung im Ordner %1.")
                      .arg(QDir::toNativeSeparators(report.backupDir));
        return message;
    case UpdateReport::Status::RolledBack:
        message.level = StartupMessage::Level::Warning;
        message.title = tr("Update fehlgeschlagen");
        message.text =
            tr("Das Update auf Version %1 ist fehlgeschlagen. Die bisherige Version wurde wiederhergestellt.")
                .arg(report.toVersion);
        if (!report.failedPath.isEmpty())
            message.text += u"\n\n"_s
                + tr("Die Datei %1 war nicht austauschbar, sie war vermutlich in Benutzung.")
                      .arg(report.failedPath);
        return message;
    case UpdateReport::Status::NotApplied:
        message.level = StartupMessage::Level::Warning;
        message.title = tr("Update nicht durchgeführt");
        message.text = tr("Das Update auf Version %1 wurde nicht durchgeführt. Es wurde nichts verändert.")
                           .arg(report.toVersion);
        if (!report.detail.isEmpty())
            message.text += u"\n\n"_s + tr("Technischer Grund: %1").arg(report.detail);
        return message;
    case UpdateReport::Status::Failed:
        message.level = StartupMessage::Level::Critical;
        message.title = tr("Update fehlgeschlagen");
        message.text =
            tr("Das Update auf Version %1 ist fehlgeschlagen, und die bisherige Version konnte nicht "
               "vollständig wiederhergestellt werden. "
               "Beim nächsten Start versucht %2 es noch einmal. Die ersetzten Dateien liegen im Ordner %3.")
                .arg(report.toVersion, product, QDir::toNativeSeparators(report.backupDir));
        return message;
    }
    return std::nullopt;
}

std::optional<StartupMessage> recoveryMessage(const TransactionResult& result)
{
    StartupMessage message;
    switch (result.outcome) {
    case TransactionOutcome::Success:
        message.title = tr("Update abgeschlossen");
        message.text = tr("Ein unterbrochenes Update wurde abgeschlossen.");
        return message;
    case TransactionOutcome::RolledBack:
        message.level = StartupMessage::Level::Warning;
        message.title = tr("Update zurückgenommen");
        message.text = tr("Ein Update wurde unterbrochen und deshalb zurückgenommen. Es gilt wieder die "
                          "bisherige Version.");
        return message;
    case TransactionOutcome::NotApplied:
        message.level = StartupMessage::Level::Warning;
        message.title = tr("Update");
        message.text = tr("Von einem unterbrochenen Update blieb eine Notiz zurück, die nicht gelesen werden "
                          "konnte. Sie wurde beiseite gelegt, es wurde nichts verändert.");
        return message;
    case TransactionOutcome::RollbackFailed:
        message.level = StartupMessage::Level::Critical;
        message.title = tr("Update fehlgeschlagen");
        message.text = tr("Ein unterbrochenes Update konnte nicht vollständig zurückgenommen werden. "
                          "Einzelheiten stehen im Protokoll. Beim nächsten Start versucht %1 es noch einmal.")
                           .arg(branding::productName());
        return message;
    case TransactionOutcome::NothingToDo:
    case TransactionOutcome::Interrupted:
        break;
    }
    return std::nullopt;
}

QString exitText(const ProcessExit& exit)
{
    const QString code = u"0x"_s + QString::number(static_cast<quint32>(exit.exitCode), 16).toUpper();
    if (exit.crashed())
        return tr("Das Projekt wurde unerwartet beendet (Fehlercode %1).").arg(code);
    return tr("Das Projekt hat sich nach %1 s mit dem Fehlercode %2 beendet.").arg(exit.runSeconds).arg(code);
}

} // namespace launcher::ui
