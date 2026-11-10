#include "ui/DownloadsPage.h"

#include "ui/Controls.h"
#include "ui/Theme.h"

#include <QVBoxLayout>

namespace launcher::ui {

DownloadsPage::DownloadsPage(QWidget* parent)
    : Page(tr("Downloads"), tr("Laufende und abgeschlossene Downloads."), parent)
{
    auto* card = new Card(this);
    card->body()->addWidget(new EmptyState(Glyph::Download, tr("Keine Downloads"),
        tr("Sobald du ein Projekt installierst oder aktualisierst, siehst du den Fortschritt hier."), card));
    contentLayout()->addWidget(card);
    contentLayout()->addStretch(1);
}

} // namespace launcher::ui
