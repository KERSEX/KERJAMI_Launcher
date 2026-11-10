#include "ui/Dialogs.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>

namespace launcher::ui::Dialogs {

namespace {

std::function<bool(const Project&)> g_testAnswer;

QString tr(const char* text)
{
    return QCoreApplication::translate("Dialogs", text);
}

} // namespace

void setUninstallConfirmationForTests(std::function<bool(const Project&)> answer)
{
    g_testAnswer = std::move(answer);
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
