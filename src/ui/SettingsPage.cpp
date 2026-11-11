#include "ui/SettingsPage.h"

#include "app/Branding.h"
#include "core/Format.h"
#include "core/PathManager.h"
#include "core/SettingsManager.h"
#include "downloads/DownloadManager.h"
#include "installer/InstallService.h"
#include "ui/Controls.h"
#include "ui/Dialogs.h"
#include "ui/LauncherUpdatePanel.h"
#include "updater/LauncherUpdateManager.h"

#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

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

SettingsPage::SettingsPage(const AppContext& context, QWidget* parent)
    : Page(tr("Einstellungen"), tr("Änderungen werden sofort gespeichert."), parent)
    , m_context(context)
    , m_settings(context.settings)
{
    const PathManager& paths = context.paths;
    LauncherUpdateManager& launcherUpdates = context.launcherUpdates;

    contentLayout()->addWidget(createLabel(tr("Speicherort"), "section", this));
    contentLayout()->addWidget(createStorageCard(paths));

    contentLayout()->addSpacing(8);
    contentLayout()->addWidget(createLabel(tr("Speicherplatz"), "section", this));
    contentLayout()->addWidget(createUsageCard());

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

SettingsPage::~SettingsPage()
{
    // A measuring or cleaning thread delivers its result to this page: it must be over first.
    m_stopping = true;
    for (QThread* thread : std::as_const(m_threads)) {
        thread->wait();
        delete thread;
    }
}

void SettingsPage::showEvent(QShowEvent* event)
{
    Page::showEvent(event);
    measureUsage();
    updateCleanButtons();
}

QWidget* SettingsPage::createUsageCard()
{
    auto* card = new Card(this);

    // A size and a button on the right of a row.
    const auto control = [card](QLabel* size, QPushButton* button) {
        auto* box = new QWidget(card);
        auto* layout = new QHBoxLayout(box);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(14);
        layout->addWidget(size);
        layout->addWidget(button);
        return box;
    };

    m_downloadsSize = createLabel(tr("wird berechnet …"), "secondary", card);
    m_downloadsSize->setObjectName("UsageDownloads");
    m_cleanDownloads = createButton(tr("Aufräumen"), card);
    m_cleanDownloads->setObjectName("CleanDownloads");
    connect(m_cleanDownloads, &QPushButton::clicked, this,
        [this] { cleanUp(Dialogs::StorageCleanup::DownloadsAndTemp); });
    card->body()->addWidget(createRow(tr("Downloads und temporäre Dateien"),
        tr("Heruntergeladene Archive und Zwischenstände. Sie werden nur für Installationen gebraucht."),
        control(m_downloadsSize, m_cleanDownloads)));

    card->body()->addWidget(createDivider(card));

    m_backupsSize = createLabel(tr("wird berechnet …"), "secondary", card);
    m_backupsSize->setObjectName("UsageBackups");
    m_cleanBackups = createButton(tr("Löschen"), card);
    m_cleanBackups->setObjectName("CleanBackups");
    connect(
        m_cleanBackups, &QPushButton::clicked, this, [this] { cleanUp(Dialogs::StorageCleanup::Backups); });
    card->body()->addWidget(createRow(tr("Sicherungen früherer Versionen"),
        tr("Bei jedem Update bleiben die zwei letzten Versionen eines Projekts erhalten. Installierte "
           "Versionen werden nie angefasst."),
        control(m_backupsSize, m_cleanBackups)));

    m_usageProblem = createLabel(QString(), "error", card);
    m_usageProblem->setObjectName("UsageProblem");
    m_usageProblem->setWordWrap(true);
    m_usageProblem->setVisible(false);
    card->body()->addWidget(m_usageProblem);

    // The buttons depend on whether anything is running.
    connect(
        &m_context.installService, &InstallService::statusChanged, this, [this] { updateCleanButtons(); });
    connect(&m_context.downloads, &DownloadManager::stateChanged, this, [this] { updateCleanButtons(); });
    connect(&m_context.launcherUpdates, &LauncherUpdateManager::infoChanged, this,
        [this] { updateCleanButtons(); });
    updateCleanButtons();
    return card;
}

void SettingsPage::runInBackground(std::function<QString()> work, std::function<void(const QString&)> done)
{
    QThread* thread = QThread::create([this, work = std::move(work), done = std::move(done)] {
        const QString result = work();
        if (m_stopping)
            return;
        QMetaObject::invokeMethod(this, [done, result] { done(result); }, Qt::QueuedConnection);
    });
    m_threads.append(thread);
    connect(thread, &QThread::finished, this, [this, thread] {
        m_threads.removeOne(thread);
        thread->deleteLater();
    });
    thread->start();
}

void SettingsPage::measureUsage()
{
    const PathManager& paths = m_context.paths;
    // The helper carries text; the sizes travel in a shared record next to it.
    const auto measured = std::make_shared<Storage::Usage>();
    runInBackground(
        [&paths, measured, this] {
            *measured = Storage::measure(paths, &m_stopping);
            return QString();
        },
        [this, measured](const QString&) { showUsage(*measured); });
}

void SettingsPage::showUsage(const Storage::Usage& usage)
{
    m_usage = usage;
    m_usageKnown = true;
    m_downloadsSize->setText(format::bytes(usage.downloadsAndTemp));
    m_backupsSize->setText(format::bytes(usage.backups));
    updateCleanButtons();
}

bool SettingsPage::cleanUpAllowed() const
{
    using State = LauncherUpdateState;
    const State update = m_context.launcherUpdates.info().state;
    const bool updating = update == State::Downloading || update == State::Preparing || update == State::Ready
        || update == State::Applying;
    return !m_cleaning && m_context.installService.busyCount() == 0 && m_context.downloads.activeCount() == 0
        && !updating;
}

void SettingsPage::updateCleanButtons()
{
    if (!m_cleanDownloads)
        return;
    const bool allowed = cleanUpAllowed();
    const QString why = tr("Das geht erst, wenn alle Downloads, Installationen und Updates beendet sind.");
    m_cleanDownloads->setEnabled(allowed && m_usageKnown && m_usage.downloadsAndTemp > 0);
    m_cleanBackups->setEnabled(allowed && m_usageKnown && m_usage.backups > 0);
    m_cleanDownloads->setToolTip(allowed ? QString() : why);
    m_cleanBackups->setToolTip(allowed ? QString() : why);
}

void SettingsPage::cleanUp(Dialogs::StorageCleanup what)
{
    if (!cleanUpAllowed())
        return;
    const bool backups = what == Dialogs::StorageCleanup::Backups;
    const qint64 size = backups ? m_usage.backups : m_usage.downloadsAndTemp;
    if (!Dialogs::confirmStorageCleanup(this, what, format::bytes(size)))
        return;

    m_cleaning = true;
    m_usageProblem->setVisible(false);
    updateCleanButtons();

    const PathManager& paths = m_context.paths;
    runInBackground(
        [&paths, backups] {
            QString error;
            const bool ok = backups ? Storage::clearBackups(paths, &error)
                                    : Storage::clearDownloadsAndTemp(paths, &error);
            return ok ? QString() : (error.isEmpty() ? QStringLiteral("?") : error);
        },
        [this](const QString& error) {
            m_cleaning = false;
            if (!error.isEmpty()) {
                m_usageProblem->setText(tr("Nicht alles konnte gelöscht werden: %1").arg(error));
                m_usageProblem->setVisible(true);
            }
            measureUsage();
            updateCleanButtons();
        });
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
