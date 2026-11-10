#include "ui/Dialogs.h"

#include "updater/ProjectUpdateManager.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>

namespace launcher::ui::Dialogs {

namespace {

std::function<bool(const Project&)> g_testAnswer;
std::function<bool(const Project&, const UpdateInfo&)> g_updateAnswer;

QString tr(const char* text)
{
    return QCoreApplication::translate("Dialogs", text);
}

} // namespace

void setUninstallConfirmationForTests(std::function<bool(const Project&)> answer)
{
    g_testAnswer = std::move(answer);
}

void setUpdateConfirmationForTests(std::function<bool(const Project&, const UpdateInfo&)> answer)
{
    g_updateAnswer = std::move(answer);
}

bool confirmUpdate(QWidget* parent, const Project& project, const UpdateInfo& update)
{
    if (g_updateAnswer)
        return g_updateAnswer(project, update);

    QMessageBox box(QMessageBox::Question, tr("Aktualisieren"),
        tr("%1 auf Version %2 aktualisieren?").arg(project.name, update.latestVersion), QMessageBox::NoButton,
        parent);
    box.setInformativeText(
        tr("Installiert ist Version %1. Die neue Version wird heruntergeladen, geprüft und installiert. "
           "Läuft das Projekt, wird die Installation vorgemerkt und beginnt, sobald es beendet ist. "
           "Die bisherige Version bleibt als Sicherung erhalten.")
            .arg(update.installedVersion));
    // The release notes are the changelog; they are shown on request.
    if (!update.notes.trimmed().isEmpty())
        box.setDetailedText(update.notes);
    QPushButton* install = box.addButton(tr("Aktualisieren"), QMessageBox::AcceptRole);
    QPushButton* keep = box.addButton(tr("Abbrechen"), QMessageBox::RejectRole);
    box.setDefaultButton(install);
    box.setEscapeButton(keep);
    box.exec();
    return box.clickedButton() == install;
}

bool confirmUninstall(QWidget* parent, const Project& project)
{
    if (g_testAnswer)
        return g_testAnswer(project);

    QMessageBox box(QMessageBox::Warning, tr("Deinstallieren"),
        tr("%1 wird deinstalliert.").arg(project.name), QMessageBox::NoButton, parent);
    box.setInformativeText(
        tr("Der Ordner des Projekts in der Bibliothek wird mit allen Dateien und allen "
           "gesicherten früheren Versionen gelöscht. Das kann nicht rückgängig gemacht werden. "
           "Andere Projekte und deine Einstellungen bleiben unberührt."));
    QPushButton* remove = box.addButton(tr("Deinstallieren"), QMessageBox::DestructiveRole);
    QPushButton* keep = box.addButton(tr("Abbrechen"), QMessageBox::RejectRole);
    box.setDefaultButton(keep);
    box.setEscapeButton(keep);
    box.exec();
    return box.clickedButton() == remove;
}

} // namespace launcher::ui::Dialogs
