#include "ui/DashboardPage.h"

#include "app/Branding.h"
#include "ui/Controls.h"
#include "ui/LauncherUpdatePanel.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace launcher::ui {

DashboardPage::DashboardPage(const AppContext& context, QWidget* parent)
    : Page(tr("Dashboard"), tr("Dein Überblick über Projekte und Updates."), parent)
{
    auto* welcome = new Card(this);
    auto* welcomeRow = new QHBoxLayout;
    welcomeRow->setSpacing(28);

    auto* welcomeTexts = new QVBoxLayout;
    welcomeTexts->setSpacing(6);
    welcomeTexts->addStretch(1);
    welcomeTexts->addWidget(
        createLabel(tr("Willkommen bei %1").arg(branding::productName()), "hero", welcome));
    QLabel* intro = createLabel(
        tr("Freigegebene Projekte entdecken, installieren, starten und aktuell halten. Alles an einem Ort."),
        "secondary", welcome);
    intro->setWordWrap(true);
    welcomeTexts->addWidget(intro);
    welcomeTexts->addStretch(1);
    welcomeRow->addLayout(welcomeTexts, 1);

    auto* logo = new QLabel(welcome);
    logo->setObjectName("WelcomeLogo");
    logo->setPixmap(Theme::brandImage(u"logo"_s, 104, devicePixelRatioF()));
    welcomeRow->addWidget(logo, 0, Qt::AlignVCenter);

    welcome->body()->addLayout(welcomeRow);
    contentLayout()->addWidget(welcome);

    contentLayout()->addSpacing(8);
    contentLayout()->addWidget(createLabel(tr("Installierte Projekte"), "section", this));

    auto* installed = new Card(this);
    auto* empty = new EmptyState(Glyph::Library, tr("Noch keine Projekte installiert"),
        tr("In der Bibliothek findest du alle freigegebenen Projekte."), installed);
    QPushButton* openLibrary = empty->addAction(tr("Zur Bibliothek"));
    openLibrary->setObjectName("OpenLibrary");
    connect(openLibrary, &QPushButton::clicked, this, &DashboardPage::libraryRequested);
    installed->body()->addWidget(empty);
    contentLayout()->addWidget(installed);

    contentLayout()->addSpacing(8);
    contentLayout()->addWidget(createLabel(tr("Launcher"), "section", this));

    auto* launcherCard = new Card(this);
    auto* versionRow = new QHBoxLayout;
    versionRow->addWidget(createLabel(tr("Installierte Version"), "body", launcherCard));
    versionRow->addStretch(1);
    QLabel* version = createLabel(branding::version(), "secondary", launcherCard);
    version->setObjectName("LauncherVersion");
    versionRow->addWidget(version);
    launcherCard->body()->addLayout(versionRow);
    auto* updates = new LauncherUpdatePanel(context.launcherUpdates, launcherCard);
    updates->setObjectName("DashboardLauncherUpdate");
    launcherCard->body()->addWidget(updates);
    contentLayout()->addWidget(launcherCard);

    contentLayout()->addStretch(1);
}

} // namespace launcher::ui
