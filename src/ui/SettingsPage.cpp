#include "ui/SettingsPage.h"

#include "app/Branding.h"
#include "core/PathManager.h"
#include "core/SettingsManager.h"
#include "ui/Controls.h"
#include "ui/LauncherUpdatePanel.h"

#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace launcher::ui {

namespace {

void openFolder(const QString& path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QPushButton* createButton(const QString& text, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

} // namespace

SettingsPage::SettingsPage(const PathManager& paths, SettingsManager& settings,
    LauncherUpdateManager& launcherUpdates, QWidget* parent)
    : Page(tr("Einstellungen"), tr("Änderungen werden sofort gespeichert."), parent)
    , m_settings(settings)
{
    contentLayout()->addWidget(createLabel(tr("Speicherort"), "section", this));
    contentLayout()->addWidget(createStorageCard(paths));

    contentLayout()->addSpacing(8);
    contentLayout()->addWidget(createLabel(tr("Updates"), "section", this));
    contentLayout()->addWidget(createUpdatesCard());

    contentLayout()->addSpacing(8);
    contentLayout()->addWidget(createLabel(tr("Start"), "section", this));
    contentLayout()->addWidget(createStartupCard());

    contentLayout()->addSpacing(8);
    contentLayout()->addWidget(createLabel(tr("Über"), "section", this));
    contentLayout()->addWidget(createAboutCard(paths, launcherUpdates));

    contentLayout()->addStretch(1);
}

QWidget* SettingsPage::createStorageCard(const PathManager& paths)
{
    auto* card = new Card(this);

    auto* openButton = createButton(tr("Ordner öffnen"), card);
    openButton->setObjectName("OpenRootFolder");
    const QString root = paths.root();
    connect(openButton, &QPushButton::clicked, this, [root] { openFolder(root); });

    card->body()->addWidget(createRow(tr("Programmordner"),
        tr("%1 ist portabel: Einstellungen, Projekte und Downloads liegen in diesem Ordner. "
           "Du kannst ihn als Ganzes verschieben.")
            .arg(branding::productName()),
        openButton));

    auto* pathLabel = new ElidedLabel(QDir::toNativeSeparators(root), card);
    pathLabel->setObjectName("RootPath");
    pathLabel->setProperty("role", QStringLiteral("body"));
    card->body()->addWidget(pathLabel);
    return card;
}

QWidget* SettingsPage::createUpdatesCard()
{
    auto* card = new Card(this);
    const LauncherSettings& current = m_settings.settings();

    const QString intervalTitle = tr("Prüfintervall");
    auto* intervalBox = new ComboBox(card);
    intervalBox->setObjectName("CheckInterval");
    intervalBox->setAccessibleName(intervalTitle);
    intervalBox->addItem(tr("Alle 15 Minuten"), 15);
    intervalBox->addItem(tr("Alle 30 Minuten"), 30);
    intervalBox->addItem(tr("Stündlich"), 60);
    intervalBox->addItem(tr("Alle 3 Stunden"), 180);
    intervalBox->addItem(tr("Alle 6 Stunden"), 360);
    intervalBox->addItem(tr("Alle 12 Stunden"), 720);
    intervalBox->addItem(tr("Täglich"), 1440);
    int intervalIndex = intervalBox->findData(current.updateCheckIntervalMinutes);
    if (intervalIndex < 0) {
        // A value edited by hand in settings.json stays selectable.
        intervalBox->addItem(tr("Alle %1 Minuten").arg(current.updateCheckIntervalMinutes),
            current.updateCheckIntervalMinutes);
        intervalIndex = intervalBox->count() - 1;
    }
    intervalBox->setCurrentIndex(intervalIndex);
    connect(intervalBox, &QComboBox::activated, this, [this, intervalBox](int index) {
        m_settings.setUpdateCheckIntervalMinutes(intervalBox->itemData(index).toInt());
    });
    card->body()->addWidget(createRow(
        intervalTitle, tr("So oft wird nach neuen Versionen deiner Projekte gesucht."), intervalBox));

    card->body()->addWidget(createDivider(card));

    const QString launcherTitle = tr("Launcher-Updates suchen");
    auto* launcherToggle = new ToggleSwitch(card);
    launcherToggle->setObjectName("LauncherUpdateAutoCheck");
    launcherToggle->setAccessibleName(launcherTitle);
    launcherToggle->setChecked(current.launcherUpdateAutoCheck);
    connect(launcherToggle, &QAbstractButton::toggled, this,
        [this](bool enabled) { m_settings.setLauncherUpdateAutoCheck(enabled); });
    card->body()->addWidget(createRow(launcherTitle,
        tr("Sucht automatisch nach neuen Versionen von %1. Installiert wird erst nach deiner Bestätigung.")
            .arg(branding::productName()),
        launcherToggle));

    return card;
}

QWidget* SettingsPage::createStartupCard()
{
    auto* card = new Card(this);

    const QString title = tr("Startseite");
    auto* startBox = new ComboBox(card);
    startBox->setObjectName("StartPage");
    startBox->setAccessibleName(title);
    startBox->addItem(tr("Dashboard"), static_cast<int>(StartPage::Dashboard));
    startBox->addItem(tr("Bibliothek"), static_cast<int>(StartPage::Library));
    startBox->setCurrentIndex(startBox->findData(static_cast<int>(m_settings.settings().startPage)));
    connect(startBox, &QComboBox::activated, this, [this, startBox](int index) {
        m_settings.setStartPage(static_cast<StartPage>(startBox->itemData(index).toInt()));
    });
    card->body()->addWidget(createRow(title, tr("Diese Seite wird nach dem Start angezeigt."), startBox));

    return card;
}

QWidget* SettingsPage::createAboutCard(const PathManager& paths, LauncherUpdateManager& launcherUpdates)
{
    auto* card = new Card(this);

    QLabel* version = createLabel(branding::version(), "secondary", card);
    version->setObjectName("SettingsVersion");
    card->body()->addWidget(createRow(branding::displayName(), tr("Installierte Version"), version));

    auto* updates = new LauncherUpdatePanel(launcherUpdates, card);
    updates->setObjectName("SettingsLauncherUpdate");
    card->body()->addWidget(updates);

    card->body()->addWidget(createDivider(card));

    auto* logsButton = createButton(tr("Log-Verzeichnis öffnen"), card);
    logsButton->setObjectName("OpenLogFolder");
    const QString logs = paths.logsDir();
    connect(logsButton, &QPushButton::clicked, this, [logs] { openFolder(logs); });
    card->body()->addWidget(
        createRow(tr("Protokolle"), tr("Die Logdateien helfen bei der Fehlersuche."), logsButton));

    return card;
}

QWidget* SettingsPage::createRow(const QString& title, const QString& description, QWidget* control)
{
    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(24);

    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    texts->addWidget(createLabel(title, "title", row));
    QLabel* descriptionLabel = createLabel(description, "secondary", row);
    descriptionLabel->setWordWrap(true);
    texts->addWidget(descriptionLabel);
    layout->addLayout(texts, 1);

    if (control)
        layout->addWidget(control, 0, Qt::AlignVCenter);
    return row;
}

} // namespace launcher::ui
