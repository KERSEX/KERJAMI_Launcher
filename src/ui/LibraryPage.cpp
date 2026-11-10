#include "ui/LibraryPage.h"

#include "ui/Controls.h"
#include "ui/Theme.h"

#include <QVBoxLayout>

namespace launcher::ui {

LibraryPage::LibraryPage(QWidget* parent)
    : Page(tr("Bibliothek"), tr("Alle freigegebenen Projekte aus der offiziellen Registry."), parent)
{
    auto* card = new Card(this);
    card->body()->addWidget(new EmptyState(Glyph::Library, tr("Noch keine Projekte verfügbar"),
        tr("Freigegebene Projekte erscheinen hier, sobald die Registry geladen wurde."), card));
    contentLayout()->addWidget(card);
    contentLayout()->addStretch(1);
}

} // namespace launcher::ui
