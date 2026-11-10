#include "ui/OperationMessages.h"

#include "app/Branding.h"
#include "core/Format.h"

#include <QCoreApplication>

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
    }
    return {};
}

} // namespace launcher::ui
