// Main window: navigation, start page and settings controls, driven the way a
// user would. Runs on the offscreen platform, so no window appears.
//
// If LAUNCHER_TEST_SCREENSHOT_DIR is set (CTest sets it), a screenshot of every
// page is saved there for visual review.

#include "SleepyProject.h"
#include "TestHttpServer.h"
#include "app/Branding.h"
#include "core/PathManager.h"
#include "core/SettingsManager.h"
#include "downloads/DownloadManager.h"
#include "github/GitHubClient.h"
#include "github/RegistryManager.h"
#include "github/ReleaseManager.h"
#include "installer/InstallService.h"
#include "installer/InstalledProjects.h"
#include "installer/Installer.h"
#include "installer/LibraryLayout.h"
#include "installer/Uninstaller.h"
#include "process/ProcessManager.h"
#include "ui/AppContext.h"
#include "ui/Dialogs.h"
#include "ui/MainWindow.h"
#include "ui/OperationMessages.h"
#include "ui/ProjectCard.h"
#include "ui/Theme.h"
#include "updater/LauncherUpdateManager.h"
#include "updater/ProjectUpdateManager.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using launcher::DownloadManager;
using launcher::FetchOptions;
using launcher::GitHubClient;
using launcher::PathManager;
using launcher::RegistryManager;
using launcher::SettingsManager;
using launcher::ui::ProjectCard;

namespace {

/// Nothing listens here, so a refresh fails with "no connection".
const QUrl kRegistryUrl(u"http://127.0.0.1:9/projects.json"_s);

QJsonObject projectEntry(
    const QString& id, const QString& name, const QString& category, const QStringList& tags, bool featured)
{
    return {
        {u"id"_s, id},
        {u"name"_s, name},
        {u"description"_s, name + u" ist ein Beispielprojekt für die Tests der Bibliothek."_s},
        {u"owner"_s, u"KERSEX"_s},
        {u"repository"_s, u"Repo-"_s + id},
        {u"category"_s, category},
        {u"tags"_s, QJsonArray::fromStringList(tags)},
        {u"enabled"_s, true},
        {u"featured"_s, featured},
        {u"release_channel"_s, u"stable"_s},
        {u"asset_pattern"_s, id + u"-Windows-x64.zip"_s},
        {u"executable"_s, id + u".exe"_s},
        {u"install_directory"_s, id},
    };
}

/// Writes a registry cache file as RegistryManager would have left it.
bool writeRegistryCache(const QString& path, const QJsonArray& projects)
{
    const QJsonObject registry {
        {u"schema_version"_s, 1},
        {u"registry_name"_s, u"Testregistry"_s},
        {u"projects"_s, projects},
    };
    const QJsonObject cache {
        {u"cache_schema"_s, 1},
        {u"fetched_at"_s, u"2026-11-10T12:00:00Z"_s},
        {u"registry_url"_s, kRegistryUrl.toString()},
        {u"registry"_s, registry},
    };
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(cache).toJson()) > 0;
}

QJsonArray sampleProjects()
{
    return {
        projectEntry(
            u"kers-overlay"_s, u"KERS Overlay"_s, u"Tools"_s, {u"F1"_s, u"Overlay"_s, u"Sim Racing"_s}, true),
        projectEntry(u"lap-timer"_s, u"Lap Timer"_s, u"Tools"_s, {u"Sim Racing"_s, u"Timing"_s}, false),
        projectEntry(u"pit-radio"_s, u"Pit Radio"_s, u"Audio"_s, {u"Overlay"_s, u"Voice"_s}, false),
        projectEntry(u"setup-sharing"_s, u"Setup Sharing"_s, u"Community"_s, {u"Setups"_s}, true),
    };
}

} // namespace
using launcher::StartPage;
using launcher::ui::MainWindow;
using PageId = launcher::ui::MainWindow::PageId;

class UiSmokeTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void styleSheetIsLoadedWithAllTokensResolved();
    void startsOnDashboard();
    void startPageSettingIsRespected();
    void sidebarSwitchesPages();
    void dashboardLinksToLibrary();
    void brandingComesFromConfiguration();
    void brandImagesAreEmbedded();
    void settingsControlsShowStoredValues();
    void settingsControlsPersistChanges();
    void rendersEveryPage();

    void libraryExplainsThatNothingWasLoaded();
    void libraryShowsProjectsFromTheCache();
    void libraryFiltersCombine();
    void libraryKeepsTheStoredListWhenRefreshFails();
    void libraryShowsAnErrorWithoutAStoredList();
    void libraryRefreshButtonShowsLoadingState();

    void cardOffersInstallationForANewProject();
    void cardShowsAnInstalledProject();
    void cardAsksBeforeUninstalling();
    void cardShowsWhyInstallingFailed();
    void cardAnnouncesAnUpdateAndAsksBeforeStarting();
    void cardLetsTheUserChooseTheUpdateMode();
    void cardShowsARunningProjectAndAnUnexpectedEnd();

    void launcherUpdatePanelsFollowTheManager();
    void launcherUpdateAsksBeforeDownloadingAndCanBeCancelled();
    void launcherUpdateFailuresAreExplained();
    void startupMessagesAreWorded();

    void downloadsPageStartsEmpty();
    void downloadsPageShowsProgressAndCancels();
    void downloadsPageShowsFinishedAndFailedDownloads();
    void downloadsPageClearsTheHistory();

private:
    launcher::ui::AppContext context()
    {
        return {*m_paths, *m_settings, *m_registry, *m_downloads, *m_installed, *m_service, *m_processes,
            *m_updates, *m_launcherUpdates};
    }

    launcher::DownloadRequest downloadRequest(
        const QString& path, const QString& fileName, qint64 size, const QString& title) const
    {
        launcher::DownloadRequest request;
        request.projectId = u"demo"_s;
        request.title = title;
        request.url = m_server->url(path);
        request.fileName = fileName;
        request.expectedSize = size;
        return request;
    }

    template <typename T> static T* find(QWidget& parent, const char* objectName)
    {
        return parent.findChild<T*>(QLatin1StringView(objectName));
    }

    /// Ids of the project cards in the order they are shown.
    static QStringList shownProjects(QWidget& window)
    {
        QStringList ids;
        for (const ProjectCard* card : window.findChildren<ProjectCard*>())
            ids.append(card->projectId());
        return ids;
    }

    /// Selects an entry of a filter box the way a click on it would.
    static void choose(QComboBox* box, const QString& text)
    {
        const int index = box->findText(text);
        QVERIFY2(index >= 0, qPrintable(text));
        box->setCurrentIndex(index);
        emit box->activated(index);
    }

    void saveScreenshot(QWidget& window, const QString& name) const
    {
        const QString dir = qEnvironmentVariable("LAUNCHER_TEST_SCREENSHOT_DIR");
        if (dir.isEmpty())
            return;
        // Widgets that were just created are laid out and shown on the next
        // turn of the event loop; give them that turn before taking the picture.
        QTest::qWait(150);
        QDir().mkpath(dir);
        window.grab().save(dir + u'/' + name + u".png"_s);
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<PathManager> m_paths;
    std::unique_ptr<SettingsManager> m_settings;
    std::unique_ptr<GitHubClient> m_client;
    std::unique_ptr<RegistryManager> m_registry;
    std::unique_ptr<DownloadManager> m_downloads;
    std::unique_ptr<TestHttpServer> m_server;
    std::unique_ptr<launcher::LibraryLayout> m_layout;
    std::unique_ptr<launcher::InstalledProjects> m_installed;
    std::unique_ptr<launcher::ReleaseManager> m_releases;
    std::unique_ptr<launcher::Installer> m_installer;
    std::unique_ptr<launcher::Uninstaller> m_uninstaller;
    std::unique_ptr<launcher::InstallService> m_service;
    std::unique_ptr<launcher::ProcessManager> m_processes;
    std::unique_ptr<launcher::ProjectUpdateManager> m_updates;
    std::unique_ptr<launcher::LauncherUpdateManager> m_launcherUpdates;
    QByteArray m_releaseBody; ///< What the test server answers to a release list request.
};

void UiSmokeTests::initTestCase()
{
    launcher::ui::Theme::apply(*qApp, launcher::ThemeId::Dark);
}

void UiSmokeTests::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_paths = std::make_unique<PathManager>(m_dir->path());
    QVERIFY(m_paths->prepare().ok());
    m_settings = std::make_unique<SettingsManager>(m_paths->settingsFile());
    m_settings->load();

    // A client that may talk to the local machine only. Tests never reach GitHub.
    m_client = std::make_unique<GitHubClient>(GitHubClient::Policy {{u"http"_s}, {u"127.0.0.1"_s}, true});
    m_registry = std::make_unique<RegistryManager>(*m_client, m_paths->registryCacheFile(), kRegistryUrl);
    FetchOptions quick;
    quick.maxAttempts = 1;
    quick.timeoutMs = 2000;
    m_registry->setFetchOptions(quick);
    m_downloads = std::make_unique<DownloadManager>(*m_client, m_paths->tempDir(), m_paths->downloadsDir());
    m_downloads->setFetchOptions(quick);

    // "/slow" delivers a third of the file and then stalls, "/missing" is gone, anything else is small.
    m_releaseBody.clear();
    m_server = std::make_unique<TestHttpServer>([this](const TestHttpServer::Request& request) {
        TestHttpServer::Response response;
        // A test that wants a release list sets m_releaseBody; without one the answer is no JSON.
        if (!m_releaseBody.isEmpty() && request.path.startsWith(u"/repos/"_s)) {
            response.body = m_releaseBody;
            return response;
        }
        if (request.path == u"/missing"_s) {
            response.status = 404;
            response.body = "Not Found";
            return response;
        }
        const bool slow = request.path == u"/slow"_s;
        response.body = QByteArray(slow ? 600 * 1024 : 50 * 1024, 'x');
        if (slow) {
            response.behavior = TestHttpServer::Response::Behavior::SendPartialThenHang;
            response.partialBytes = 200 * 1024;
        }
        return response;
    });
    QVERIFY(m_server->listen());

    m_layout = std::make_unique<launcher::LibraryLayout>(m_paths->libraryDir(), m_paths->tempDir());
    m_installed = std::make_unique<launcher::InstalledProjects>(m_paths->stateFile());
    m_installed->load();
    m_releases = std::make_unique<launcher::ReleaseManager>(
        *m_client, launcher::ReleaseEndpoints {m_server->url(), m_server->url()});
    m_releases->setFetchOptions(quick);
    m_installer = std::make_unique<launcher::Installer>(*m_layout, *m_installed);
    m_uninstaller = std::make_unique<launcher::Uninstaller>(*m_layout, *m_installed);
    m_service = std::make_unique<launcher::InstallService>(
        *m_client, *m_releases, *m_downloads, *m_installer, *m_uninstaller, *m_installed, *m_layout);
    m_processes = std::make_unique<launcher::ProcessManager>(*m_layout, *m_installed);
    m_processes->setPolling(false); // the tests decide when to look
    // As in the application: projects are started and checked through the process manager.
    m_service->setStarter([this](const QString& id) { return m_processes->start(id); });
    m_installer->setRunningCheck([this](const QString& id) { return m_processes->isRunningNow(id); });
    m_uninstaller->setRunningCheck([this](const QString& id) { return m_processes->isRunningNow(id); });
    m_updates = std::make_unique<launcher::ProjectUpdateManager>(
        *m_client, *m_registry, *m_releases, *m_service, *m_processes, *m_installed, *m_settings);
    m_launcherUpdates = std::make_unique<launcher::LauncherUpdateManager>(
        *m_client, *m_releases, *m_downloads, *m_settings, *m_paths);
    m_launcherUpdates->setCurrentVersion(u"1.0.0"_s);
    m_launcherUpdates->setUpdaterStarter([](const QString&, const QString&) { return true; });
}

void UiSmokeTests::cleanup()
{
    m_launcherUpdates.reset();
    m_updates.reset();
    m_processes.reset();
    m_service.reset();
    m_uninstaller.reset();
    m_installer.reset();
    m_releases.reset();
    m_installed.reset();
    m_layout.reset();
    m_server.reset();
    m_downloads.reset();
    m_registry.reset();
    m_client.reset();
    m_settings.reset();
    m_paths.reset();
    m_dir.reset();
}

void UiSmokeTests::styleSheetIsLoadedWithAllTokensResolved()
{
    const QString sheet = qApp->styleSheet();
    QVERIFY(sheet.contains(u"#Sidebar"_s));
    // Every "@token" must have been replaced by a colour.
    QVERIFY2(!sheet.contains(u'@'), "unresolved colour token in app.qss");
    // The accent colour of the design system made it into the sheet.
    QVERIFY(sheet.contains(u"#00c8ff"_s, Qt::CaseInsensitive));
}

void UiSmokeTests::startsOnDashboard()
{
    MainWindow window(context());
    QCOMPARE(window.currentPage(), PageId::Dashboard);
    QVERIFY(find<QAbstractButton>(window, "NavDashboard")->isChecked());
    QVERIFY(!find<QAbstractButton>(window, "NavLibrary")->isChecked());
}

void UiSmokeTests::startPageSettingIsRespected()
{
    QVERIFY(m_settings->setStartPage(StartPage::Library));

    MainWindow window(context());
    QCOMPARE(window.currentPage(), PageId::Library);
    QVERIFY(find<QAbstractButton>(window, "NavLibrary")->isChecked());
    QVERIFY(!find<QAbstractButton>(window, "NavDashboard")->isChecked());
}

void UiSmokeTests::sidebarSwitchesPages()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QList<std::pair<const char*, PageId>> entries = {
        {"NavLibrary", PageId::Library},
        {"NavDownloads", PageId::Downloads},
        {"NavSettings", PageId::Settings},
        {"NavDashboard", PageId::Dashboard},
    };
    for (const auto& [objectName, page] : entries) {
        auto* button = find<QAbstractButton>(window, objectName);
        QVERIFY2(button, objectName);
        QTest::mouseClick(button, Qt::LeftButton);
        QCOMPARE(window.currentPage(), page);

        // Exactly the clicked entry is marked as active.
        for (const auto& [otherName, otherPage] : entries)
            QCOMPARE(find<QAbstractButton>(window, otherName)->isChecked(), otherPage == page);
    }
}

void UiSmokeTests::dashboardLinksToLibrary()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto* button = find<QAbstractButton>(window, "OpenLibrary");
    QVERIFY(button);
    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(window.currentPage(), PageId::Library);
    QVERIFY(find<QAbstractButton>(window, "NavLibrary")->isChecked());
}

void UiSmokeTests::brandingComesFromConfiguration()
{
    MainWindow window(context());
    QCOMPARE(window.windowTitle(), launcher::branding::displayName());
    QCOMPARE(find<QLabel>(window, "BrandName")->text(), launcher::branding::productName());
    QVERIFY(find<QLabel>(window, "SidebarVersion")->text().contains(launcher::branding::version()));
    QCOMPARE(find<QLabel>(window, "LauncherVersion")->text(), launcher::branding::version());
}

void UiSmokeTests::brandImagesAreEmbedded()
{
    // A missing or misnamed resource would only show up as an empty spot in the window.
    using launcher::ui::Theme::applicationIcon;
    using launcher::ui::Theme::brandImage;

    for (const QString& name : {u"emblem"_s, u"logo"_s}) {
        const QPixmap image = brandImage(name, 40, 2.0);
        QVERIFY2(!image.isNull(), qPrintable(name));
        QCOMPARE(image.height(), 80); // rendered for a 2x screen
        QCOMPARE(image.devicePixelRatio(), 2.0);
        QVERIFY(image.hasAlphaChannel());
    }
    QVERIFY(brandImage(u"does-not-exist"_s, 40, 1.0).isNull());

    // Every size of the application icon exists as its own image.
    const QIcon icon = applicationIcon();
    QVERIFY(!icon.isNull());
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        const QPixmap pixmap = icon.pixmap(QSize(size, size));
        QVERIFY2(!pixmap.isNull(), qPrintable(QString::number(size)));
        QCOMPARE(pixmap.size(), QSize(size, size));
    }

    // The sidebar and the dashboard actually show the images.
    MainWindow window(context());
    QVERIFY(!find<QLabel>(window, "BrandEmblem")->pixmap().isNull());
    QVERIFY(!find<QLabel>(window, "WelcomeLogo")->pixmap().isNull());
    QVERIFY(!window.windowIcon().isNull());
}

void UiSmokeTests::settingsControlsShowStoredValues()
{
    QVERIFY(m_settings->setRegistryAutoCheck(false));
    QVERIFY(m_settings->setUpdateCheckIntervalMinutes(360));
    QVERIFY(m_settings->setStartPage(StartPage::Library));

    MainWindow window(context());
    QVERIFY(!find<QAbstractButton>(window, "RegistryAutoCheck")->isChecked());
    QVERIFY(find<QAbstractButton>(window, "LauncherUpdateAutoCheck")->isChecked());
    QCOMPARE(find<QComboBox>(window, "CheckInterval")->currentData().toInt(), 360);
    QCOMPARE(
        find<QComboBox>(window, "StartPage")->currentData().toInt(), static_cast<int>(StartPage::Library));
    QCOMPARE(find<QLabel>(window, "RootPath")->text(), QDir::toNativeSeparators(m_paths->root()));
}

void UiSmokeTests::settingsControlsPersistChanges()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Settings);
    QTest::qWait(250); // let the page transition finish

    auto* registryToggle = find<QAbstractButton>(window, "RegistryAutoCheck");
    QVERIFY(registryToggle->isChecked());
    QTest::mouseClick(registryToggle, Qt::LeftButton);
    QVERIFY(!registryToggle->isChecked());

    auto* launcherToggle = find<QAbstractButton>(window, "LauncherUpdateAutoCheck");
    QTest::keyClick(launcherToggle, Qt::Key_Space);
    QVERIFY(!launcherToggle->isChecked());

    auto* interval = find<QComboBox>(window, "CheckInterval");
    QCOMPARE(interval->currentData().toInt(), 60);
    QTest::keyClick(interval, Qt::Key_Down);
    QCOMPARE(interval->currentData().toInt(), 180);

    auto* startPage = find<QComboBox>(window, "StartPage");
    QTest::keyClick(startPage, Qt::Key_Down);

    // What the next start of the launcher would read from disk.
    SettingsManager restarted(m_paths->settingsFile());
    QCOMPARE(restarted.load(), SettingsManager::LoadResult::Loaded);
    QVERIFY(!restarted.settings().registryAutoCheck);
    QVERIFY(!restarted.settings().launcherUpdateAutoCheck);
    QCOMPARE(restarted.settings().updateCheckIntervalMinutes, 180);
    QCOMPARE(restarted.settings().startPage, StartPage::Library);
}

void UiSmokeTests::rendersEveryPage()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QString outputDir = qEnvironmentVariable("LAUNCHER_TEST_SCREENSHOT_DIR");
    if (!outputDir.isEmpty())
        QVERIFY(QDir().mkpath(outputDir));

    const QList<std::pair<PageId, QString>> pages = {
        {PageId::Dashboard, u"dashboard"_s},
        {PageId::Library, u"library"_s},
        {PageId::Downloads, u"downloads"_s},
        {PageId::Settings, u"settings"_s},
    };
    for (const auto& [page, name] : pages) {
        window.showPage(page);
        QTest::qWait(250); // let the page transition finish

        const QImage image = window.grab().toImage();
        QVERIFY(!image.isNull());

        // A rendered page shows the window background and the accent colour.
        const launcher::ui::ThemePalette& colors = launcher::ui::Theme::palette();
        bool hasBackground = false;
        bool hasAccent = false;
        for (int y = 0; y < image.height() && !(hasBackground && hasAccent); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QRgb pixel = image.pixel(x, y) | 0xFF000000;
                hasBackground = hasBackground || pixel == colors.background.rgb();
                hasAccent = hasAccent || pixel == colors.accent.rgb();
            }
        }
        QVERIFY2(hasBackground, qPrintable(name));
        QVERIFY2(hasAccent, qPrintable(name));

        if (!outputDir.isEmpty())
            QVERIFY(image.save(outputDir + u'/' + name + u".png"_s));
    }
}

void UiSmokeTests::libraryExplainsThatNothingWasLoaded()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Library);

    QVERIFY(shownProjects(window).isEmpty());
    QVERIFY(find<QAbstractButton>(window, "LibraryRetry"));
    QVERIFY(find<QLabel>(window, "LibraryStatus")->text().isEmpty());
    QVERIFY(find<QAbstractButton>(window, "LibraryRefresh")->isEnabled());
}

void UiSmokeTests::libraryShowsProjectsFromTheCache()
{
    QVERIFY(writeRegistryCache(m_paths->registryCacheFile(), sampleProjects()));
    m_registry->loadCache();
    QCOMPARE(m_registry->projects().size(), 4);

    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Library);
    QTest::qWait(250);

    // Featured projects first, each group alphabetical.
    QCOMPARE(shownProjects(window),
        (QStringList {u"kers-overlay"_s, u"setup-sharing"_s, u"lap-timer"_s, u"pit-radio"_s}));
    QVERIFY(find<QLabel>(window, "LibraryStatus")->text().contains(u"Gespeicherte Liste"_s));
    QVERIFY(find<QLabel>(window, "LibraryStatus")->text().contains(u"4 Projekte"_s));
    QVERIFY(find<QAbstractButton>(window, "LibraryRetry") == nullptr);

    // Filter boxes list what the registry contains, once each and sorted.
    auto* category = find<QComboBox>(window, "LibraryCategory");
    QStringList categories;
    for (int i = 0; i < category->count(); ++i)
        categories.append(category->itemText(i));
    QCOMPARE(categories, (QStringList {u"Alle Kategorien"_s, u"Audio"_s, u"Community"_s, u"Tools"_s}));

    auto* tag = find<QComboBox>(window, "LibraryTag");
    QCOMPARE(tag->count(), 1 + 6); // F1, Overlay, Setups, Sim Racing, Timing, Voice
    QVERIFY(tag->findText(u"Overlay"_s) >= 0);

    // The name, a tag chip and the featured badge are shown on the card.
    QVERIFY(window.findChildren<QLabel*>(u"ProjectFeatured"_s).size() == 2);

    saveScreenshot(window, u"library-projects"_s);
}

void UiSmokeTests::libraryFiltersCombine()
{
    QVERIFY(writeRegistryCache(m_paths->registryCacheFile(), sampleProjects()));
    m_registry->loadCache();
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Library);

    auto* search = find<QLineEdit>(window, "LibrarySearch");
    auto* category = find<QComboBox>(window, "LibraryCategory");
    auto* tag = find<QComboBox>(window, "LibraryTag");
    auto* featured = find<QAbstractButton>(window, "LibraryFeaturedOnly");

    // Search looks at name, description, category and tags, ignoring case.
    search->setText(u"OVERLAY"_s);
    QCOMPARE(shownProjects(window), (QStringList {u"kers-overlay"_s, u"pit-radio"_s}));
    search->setText(u"beispielprojekt"_s); // in every description
    QCOMPARE(shownProjects(window).size(), 4);
    search->setText(u"audio"_s); // a category
    QCOMPARE(shownProjects(window), (QStringList {u"pit-radio"_s}));
    search->clear();
    QCOMPARE(shownProjects(window).size(), 4);

    choose(category, u"Tools"_s);
    QCOMPARE(shownProjects(window), (QStringList {u"kers-overlay"_s, u"lap-timer"_s}));

    choose(tag, u"Sim Racing"_s);
    QCOMPARE(shownProjects(window), (QStringList {u"kers-overlay"_s, u"lap-timer"_s}));
    choose(tag, u"Timing"_s);
    QCOMPARE(shownProjects(window), (QStringList {u"lap-timer"_s}));

    // Filters add up: a featured-only list has nothing left in this combination.
    featured->click();
    QVERIFY(shownProjects(window).isEmpty());
    auto* reset = find<QAbstractButton>(window, "LibraryResetFilters");
    QVERIFY(reset);
    saveScreenshot(window, u"library-no-results"_s);

    // Resetting brings everything back and clears every control.
    reset->click();
    QCOMPARE(shownProjects(window).size(), 4);
    QVERIFY(search->text().isEmpty());
    QCOMPARE(category->currentIndex(), 0);
    QCOMPARE(tag->currentIndex(), 0);
    QVERIFY(!featured->isChecked());

    featured->click();
    QCOMPARE(shownProjects(window), (QStringList {u"kers-overlay"_s, u"setup-sharing"_s}));
}

void UiSmokeTests::libraryKeepsTheStoredListWhenRefreshFails()
{
    QVERIFY(writeRegistryCache(m_paths->registryCacheFile(), sampleProjects()));
    m_registry->loadCache();
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Library);

    QSignalSpy finished(m_registry.get(), &RegistryManager::refreshFinished);
    m_registry->refresh();
    QVERIFY(finished.wait(10000));
    QCOMPARE(finished.at(0).at(0).toBool(), false);

    QCOMPARE(shownProjects(window).size(), 4);
    const QString status = find<QLabel>(window, "LibraryStatus")->text();
    QVERIFY2(status.contains(u"Keine Verbindung zu GitHub"_s), qPrintable(status));
    QVERIFY2(status.contains(u"gespeicherte Liste"_s), qPrintable(status));
    saveScreenshot(window, u"library-offline-with-cache"_s);
}

void UiSmokeTests::libraryShowsAnErrorWithoutAStoredList()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Library);

    QSignalSpy finished(m_registry.get(), &RegistryManager::refreshFinished);
    m_registry->refresh();
    QVERIFY(finished.wait(10000));

    QVERIFY(shownProjects(window).isEmpty());
    QVERIFY(find<QAbstractButton>(window, "LibraryRetry"));
    bool titleShown = false;
    bool reasonShown = false;
    for (const QLabel* label : window.findChildren<QLabel*>()) {
        titleShown = titleShown || label->text().contains(u"konnten nicht geladen werden"_s);
        reasonShown = reasonShown || label->text().contains(u"Keine Verbindung zu GitHub"_s);
    }
    QVERIFY(titleShown);
    QVERIFY(reasonShown);
    // The error card carries the message; the status line does not repeat it.
    QVERIFY(find<QLabel>(window, "LibraryStatus")->text().isEmpty());
    saveScreenshot(window, u"library-offline-empty"_s);
}

void UiSmokeTests::libraryRefreshButtonShowsLoadingState()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Library);

    auto* refresh = find<QAbstractButton>(window, "LibraryRefresh");
    QVERIFY(refresh->isEnabled());
    QSignalSpy finished(m_registry.get(), &RegistryManager::refreshFinished);

    refresh->click();
    QVERIFY(m_registry->isLoading());
    QVERIFY(!refresh->isEnabled());
    QVERIFY(find<QLabel>(window, "LibraryStatus")->text().contains(u"wird geladen"_s));

    QVERIFY(finished.wait(10000));
    QVERIFY(!m_registry->isLoading());
    QVERIFY(refresh->isEnabled());
}

void UiSmokeTests::downloadsPageStartsEmpty()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Downloads);

    QVERIFY(find<QWidget>(window, "DownloadsEmpty")->isVisibleTo(&window));
    QVERIFY(window.findChildren<QFrame*>(u"DownloadRow"_s).isEmpty());
}

void UiSmokeTests::downloadsPageShowsProgressAndCancels()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Downloads);

    const launcher::DownloadId id =
        m_downloads->enqueue(downloadRequest(u"/slow"_s, u"Slow.zip"_s, 600 * 1024, u"KERS Overlay 1.2.0"_s));
    QTRY_VERIFY_WITH_TIMEOUT(m_downloads->download(id)->bytesReceived > 0, 10000);
    QTest::qWait(250); // the page shows what the manager reports, a few times a second

    const QList<QFrame*> rows = window.findChildren<QFrame*>(u"DownloadRow"_s);
    QCOMPARE(rows.size(), 1);
    QVERIFY(!find<QWidget>(window, "DownloadsEmpty")->isVisibleTo(&window));
    QCOMPARE(find<QLabel>(*rows.first(), "DownloadTitle")->text(), u"KERS Overlay 1.2.0"_s);

    const QString status = find<QLabel>(*rows.first(), "DownloadStatus")->text();
    QVERIFY2(status.contains(u" von "_s), qPrintable(status));
    auto* bar = find<QProgressBar>(*rows.first(), "DownloadProgress");
    QVERIFY(bar);
    // How far the download is depends on the speed of the test server, so only the
    // direction is checked: the bar moves, and it is not full while the download runs.
    QTRY_VERIFY_WITH_TIMEOUT(bar->value() > 0, 5000);
    QVERIFY(bar->value() < bar->maximum());
    QCOMPARE(m_downloads->download(id)->state, launcher::DownloadState::Running);
    saveScreenshot(window, u"downloads-running"_s);

    // The button on the row cancels the download, and the row moves to the history.
    auto* cancel = find<QAbstractButton>(*rows.first(), "DownloadCancel");
    QVERIFY(cancel);
    cancel->click();
    QCOMPARE(m_downloads->download(id)->state, launcher::DownloadState::Cancelled);

    const QList<QFrame*> after = window.findChildren<QFrame*>(u"DownloadRow"_s);
    QCOMPARE(after.size(), 1);
    QCOMPARE(find<QLabel>(*after.first(), "DownloadStatus")->text(), u"Abgebrochen"_s);
    QVERIFY(find<QAbstractButton>(*after.first(), "DownloadCancel") == nullptr);
    QVERIFY(find<QProgressBar>(*after.first(), "DownloadProgress") == nullptr);
}

void UiSmokeTests::downloadsPageShowsFinishedAndFailedDownloads()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Downloads);

    const launcher::DownloadId good =
        m_downloads->enqueue(downloadRequest(u"/ok"_s, u"Good.zip"_s, 50 * 1024, u"Setup Sharing 2.0.1"_s));
    const launcher::DownloadId bad =
        m_downloads->enqueue(downloadRequest(u"/missing"_s, u"Bad.zip"_s, 50 * 1024, u"Lap Timer 0.4.0"_s));
    QTRY_COMPARE_WITH_TIMEOUT(m_downloads->download(good)->state, launcher::DownloadState::Finished, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(m_downloads->download(bad)->state, launcher::DownloadState::Failed, 10000);
    QTest::qWait(100);

    // The history lists the newest entry first.
    const QList<QFrame*> rows = window.findChildren<QFrame*>(u"DownloadRow"_s);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(find<QLabel>(*rows.at(0), "DownloadTitle")->text(), u"Lap Timer 0.4.0"_s);
    const QString failed = find<QLabel>(*rows.at(0), "DownloadStatus")->text();
    QVERIFY2(failed.startsWith(u"Fehlgeschlagen: "_s) && failed.contains(u"HTTP 404"_s), qPrintable(failed));

    QCOMPARE(find<QLabel>(*rows.at(1), "DownloadTitle")->text(), u"Setup Sharing 2.0.1"_s);
    QVERIFY(find<QLabel>(*rows.at(1), "DownloadStatus")->text().startsWith(u"Abgeschlossen"_s));

    // Finished and failed entries have neither a progress bar nor a cancel button.
    for (const QFrame* row : rows)
        QVERIFY(row->findChildren<QProgressBar*>().isEmpty());
    QVERIFY(window.findChildren<QAbstractButton*>(u"DownloadCancel"_s).isEmpty());
    saveScreenshot(window, u"downloads-history"_s);
}

void UiSmokeTests::downloadsPageClearsTheHistory()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Downloads);

    const launcher::DownloadId id =
        m_downloads->enqueue(downloadRequest(u"/missing"_s, u"Bad.zip"_s, 50 * 1024, u"Lap Timer 0.4.0"_s));
    QTRY_COMPARE_WITH_TIMEOUT(m_downloads->download(id)->state, launcher::DownloadState::Failed, 10000);
    QCOMPARE(window.findChildren<QFrame*>(u"DownloadRow"_s).size(), 1);

    auto* clear = find<QAbstractButton>(window, "DownloadsClear");
    QVERIFY(clear->isVisibleTo(&window));
    clear->click();

    QVERIFY(window.findChildren<QFrame*>(u"DownloadRow"_s).isEmpty());
    QVERIFY(m_downloads->downloads().isEmpty());
    QVERIFY(find<QWidget>(window, "DownloadsEmpty")->isVisibleTo(&window));
}

// ---- Project cards ------------------------------------------------------------------

namespace {

/// A record as the installer leaves it, with the file it describes.
bool installFake(const launcher::LibraryLayout& layout, launcher::InstalledProjects& installed,
    const QString& id, const QString& version)
{
    launcher::InstalledProject record;
    record.projectId = id;
    record.version = version;
    record.tag = u"v"_s + version;
    record.installDirectory = id;
    record.executable = id + u".exe"_s;
    record.installedAt = QDateTime::fromString(u"2026-11-10T12:00:00Z"_s, Qt::ISODate);

    const QString exe = layout.executablePath(record);
    QDir().mkpath(QFileInfo(exe).absolutePath());
    QFile file(exe);
    if (!file.open(QIODevice::WriteOnly) || file.write("MZ") != 2)
        return false;
    file.close();
    return installed.upsert(record);
}

template <typename T> T* findIn(QWidget& parent, const char* name)
{
    return parent.findChild<T*>(QLatin1StringView(name));
}

} // namespace

void UiSmokeTests::cardOffersInstallationForANewProject()
{
    QVERIFY(writeRegistryCache(m_paths->registryCacheFile(), sampleProjects()));
    m_registry->loadCache();

    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Library);
    QTest::qWait(250);

    auto* card = window.findChildren<ProjectCard*>().first();
    auto* primary = findIn<QPushButton>(*card, "ProjectPrimary");
    QVERIFY(primary);
    QCOMPARE(primary->text(), u"Installieren"_s);
    QVERIFY(primary->isEnabled());
    QCOMPARE(findIn<QLabel>(*card, "ProjectState")->text(), u"Nicht installiert"_s);
    QVERIFY(!findIn<QPushButton>(*card, "ProjectUninstall")->isVisible());
    QVERIFY(!findIn<QProgressBar>(*card, "ProjectProgress")->isVisible());
    QVERIFY(!findIn<QLabel>(*card, "ProjectProblem")->isVisible());
}

void UiSmokeTests::cardShowsAnInstalledProject()
{
    QVERIFY(writeRegistryCache(m_paths->registryCacheFile(), sampleProjects()));
    m_registry->loadCache();
    QVERIFY(installFake(*m_layout, *m_installed, u"lap-timer"_s, u"1.4.2"_s));

    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Library);
    QTest::qWait(250);

    ProjectCard* installedCard = nullptr;
    ProjectCard* freshCard = nullptr;
    for (ProjectCard* card : window.findChildren<ProjectCard*>())
        (card->projectId() == u"lap-timer"_s ? installedCard : freshCard) = card;
    QVERIFY(installedCard && freshCard);

    QCOMPARE(findIn<QPushButton>(*installedCard, "ProjectPrimary")->text(), u"Starten"_s);
    QCOMPARE(findIn<QLabel>(*installedCard, "ProjectState")->text(), u"Installiert: Version 1.4.2"_s);
    QVERIFY(findIn<QPushButton>(*installedCard, "ProjectUninstall")->isVisible());
    // The other cards are not affected.
    QCOMPARE(findIn<QPushButton>(*freshCard, "ProjectPrimary")->text(), u"Installieren"_s);

    saveScreenshot(window, u"library-installed"_s);

    // A change made elsewhere (a finished installation) shows up without rebuilding the page.
    QVERIFY(installFake(*m_layout, *m_installed, freshCard->projectId(), u"2.0.0"_s));
    QCOMPARE(findIn<QPushButton>(*freshCard, "ProjectPrimary")->text(), u"Starten"_s);
}

void UiSmokeTests::cardAsksBeforeUninstalling()
{
    QVERIFY(writeRegistryCache(m_paths->registryCacheFile(), sampleProjects()));
    m_registry->loadCache();
    QVERIFY(installFake(*m_layout, *m_installed, u"lap-timer"_s, u"1.4.2"_s));

    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Library);
    QTest::qWait(250);

    ProjectCard* card = nullptr;
    for (ProjectCard* candidate : window.findChildren<ProjectCard*>()) {
        if (candidate->projectId() == u"lap-timer"_s)
            card = candidate;
    }
    QVERIFY(card);
    auto* remove = findIn<QPushButton>(*card, "ProjectUninstall");

    // "No": nothing is touched.
    QString asked;
    launcher::ui::Dialogs::setUninstallConfirmationForTests([&asked](const launcher::Project& project) {
        asked = project.name;
        return false;
    });
    remove->click();
    QCOMPARE(asked, u"Lap Timer"_s);
    QVERIFY(m_installed->contains(u"lap-timer"_s));
    QVERIFY(QFileInfo::exists(m_paths->libraryDir() + u"/lap-timer/current/lap-timer.exe"_s));

    // "Yes": the project is removed and the card offers installation again.
    launcher::ui::Dialogs::setUninstallConfirmationForTests([](const launcher::Project&) { return true; });
    remove->click();
    launcher::ui::Dialogs::setUninstallConfirmationForTests({});

    QVERIFY(!m_installed->contains(u"lap-timer"_s));
    QCOMPARE(findIn<QPushButton>(*card, "ProjectPrimary")->text(), u"Installieren"_s);
    QVERIFY(!findIn<QPushButton>(*card, "ProjectUninstall")->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(m_paths->libraryDir() + u"/lap-timer"_s), 10000);
}

void UiSmokeTests::cardShowsWhyInstallingFailed()
{
    QVERIFY(writeRegistryCache(m_paths->registryCacheFile(), sampleProjects()));
    m_registry->loadCache();

    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Library);
    QTest::qWait(250);

    ProjectCard* card = window.findChildren<ProjectCard*>().first();
    auto* primary = findIn<QPushButton>(*card, "ProjectPrimary");
    auto* state = findIn<QLabel>(*card, "ProjectState");
    auto* problem = findIn<QLabel>(*card, "ProjectProblem");

    // The test server answers the release list with bytes that are no JSON.
    QSignalSpy finished(m_service.get(), &launcher::InstallService::installFinished);
    primary->click();
    QCOMPARE(primary->text(), u"Abbrechen"_s); // busy at once
    QVERIFY(findIn<QProgressBar>(*card, "ProjectProgress")->isVisible());
    QVERIFY(state->text().contains(u"Release"_s));

    QVERIFY(finished.wait(10000));
    QCOMPARE(primary->text(), u"Installieren"_s);
    QVERIFY(!findIn<QProgressBar>(*card, "ProjectProgress")->isVisible());
    QVERIFY(problem->isVisible());
    QVERIFY2(problem->text().contains(u"Release-Informationen"_s), qPrintable(problem->text()));
    QVERIFY(!m_installed->contains(card->projectId()));
    saveScreenshot(window, u"library-install-failed"_s);

    // A new attempt clears the message at once.
    primary->click();
    QVERIFY(!problem->isVisible());
    QVERIFY(finished.wait(10000));
}

// ---- Updates and running projects on the card -----------------------------------------

namespace {

/// A release list with one stable release of the given version and a digest that does not fit any file.
QByteArray releaseListFor(const QString& version, const QString& notes)
{
    const QJsonObject asset {{u"name"_s, u"lap-timer-Windows-x64.zip"_s}, {u"size"_s, 123456},
        {u"state"_s, u"uploaded"_s}, {u"digest"_s, u"sha256:"_s + u"a"_s.repeated(64)}};
    const QJsonObject release {{u"tag_name"_s, u"v"_s + version}, {u"name"_s, u"Release"_s},
        {u"body"_s, notes}, {u"draft"_s, false}, {u"prerelease"_s, false},
        {u"published_at"_s, u"2026-11-01T12:00:00Z"_s}, {u"assets"_s, QJsonArray {asset}}};
    return QJsonDocument(QJsonArray {release}).toJson();
}

} // namespace

void UiSmokeTests::cardAnnouncesAnUpdateAndAsksBeforeStarting()
{
    QVERIFY(writeRegistryCache(m_paths->registryCacheFile(), sampleProjects()));
    m_registry->loadCache();
    QVERIFY(installFake(*m_layout, *m_installed, u"lap-timer"_s, u"1.4.2"_s));
    m_releaseBody = releaseListFor(u"2.0.0"_s, u"- Neues Fahrzeugmodell\n- Fehler behoben"_s);

    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Library);
    QTest::qWait(250);

    ProjectCard* card = nullptr;
    for (ProjectCard* candidate : window.findChildren<ProjectCard*>()) {
        if (candidate->projectId() == u"lap-timer"_s)
            card = candidate;
    }
    QVERIFY(card);
    auto* badge = findIn<QLabel>(*card, "ProjectUpdateBadge");
    auto* update = findIn<QPushButton>(*card, "ProjectUpdate");
    auto* primary = findIn<QPushButton>(*card, "ProjectPrimary");

    // Before a check there is nothing to announce.
    QVERIFY(!badge->isVisible());
    QVERIFY(!update->isVisible());

    QSignalSpy checked(m_updates.get(), &launcher::ProjectUpdateManager::checkFinished);
    m_updates->checkAll();
    QVERIFY(checked.wait(10000));

    QVERIFY(badge->isVisible());
    QCOMPARE(badge->text(), u"Update 2.0.0"_s);
    QVERIFY(update->isVisible());
    QCOMPARE(update->text(), u"Aktualisieren"_s);
    QCOMPARE(primary->text(), u"Starten"_s);
    QVERIFY(findIn<QLabel>(*card, "ProjectState")->text().contains(u"1.4.2"_s));
    saveScreenshot(window, u"library-update-available"_s);

    // The user is asked, and sees the version and the changelog. "No" changes nothing.
    QString seenVersion;
    QString seenNotes;
    launcher::ui::Dialogs::setUpdateConfirmationForTests(
        [&](const launcher::Project&, const launcher::UpdateInfo& info) {
            seenVersion = info.latestVersion;
            seenNotes = info.notes;
            return false;
        });
    update->click();
    QCOMPARE(seenVersion, u"2.0.0"_s);
    QVERIFY(seenNotes.contains(u"Neues Fahrzeugmodell"_s));
    QCOMPARE(m_updates->info(u"lap-timer"_s).state, launcher::UpdateState::Available);
    QVERIFY(m_updates->active().isEmpty());

    // "Yes" starts the update: the card shows the work and offers to cancel it.
    launcher::ui::Dialogs::setUpdateConfirmationForTests(
        [](const launcher::Project&, const launcher::UpdateInfo&) { return true; });
    update->click();
    launcher::ui::Dialogs::setUpdateConfirmationForTests({});
    QVERIFY(m_updates->info(u"lap-timer"_s).state != launcher::UpdateState::Available);
    QCOMPARE(primary->text(), u"Abbrechen"_s);
    QVERIFY(!update->isVisible());

    // Cancelling brings the announcement back; nothing was installed.
    primary->click();
    QTRY_COMPARE_WITH_TIMEOUT(m_updates->info(u"lap-timer"_s).state, launcher::UpdateState::Available, 10000);
    QCOMPARE(primary->text(), u"Starten"_s);
    QVERIFY(update->isVisible());
    QCOMPARE(m_installed->get(u"lap-timer"_s)->version, u"1.4.2"_s);
}

void UiSmokeTests::cardLetsTheUserChooseTheUpdateMode()
{
    QVERIFY(writeRegistryCache(m_paths->registryCacheFile(), sampleProjects()));
    m_registry->loadCache();
    QVERIFY(installFake(*m_layout, *m_installed, u"lap-timer"_s, u"1.4.2"_s));

    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Library);
    QTest::qWait(250);

    ProjectCard* installedCard = nullptr;
    ProjectCard* freshCard = nullptr;
    for (ProjectCard* card : window.findChildren<ProjectCard*>())
        (card->projectId() == u"lap-timer"_s ? installedCard : freshCard) = card;
    QVERIFY(installedCard && freshCard);

    // Only an installed project has the choice, and every project starts as manual.
    auto* mode = findIn<launcher::ui::ComboBox>(*installedCard, "ProjectUpdateMode");
    QVERIFY(mode->isVisible());
    QCOMPARE(mode->currentText(), u"Updates: Manuell"_s);
    QVERIFY(!findIn<launcher::ui::ComboBox>(*freshCard, "ProjectUpdateMode")->isVisible());

    choose(mode, u"Updates: Automatisch"_s);
    QCOMPARE(m_settings->updateMode(u"lap-timer"_s), launcher::UpdateMode::Automatic);
    QCOMPARE(m_settings->updateMode(freshCard->projectId()), launcher::UpdateMode::Manual);

    // The choice is stored and shown again by a new window.
    MainWindow other(context());
    other.show();
    QVERIFY(QTest::qWaitForWindowExposed(&other));
    other.showPage(PageId::Library);
    QTest::qWait(250);
    for (ProjectCard* card : other.findChildren<ProjectCard*>()) {
        if (card->projectId() == u"lap-timer"_s)
            QCOMPARE(findIn<launcher::ui::ComboBox>(*card, "ProjectUpdateMode")->currentText(),
                u"Updates: Automatisch"_s);
    }

    // A change made elsewhere shows on the card without rebuilding the page.
    QVERIFY(m_settings->setUpdateMode(u"lap-timer"_s, launcher::UpdateMode::Manual));
    QCOMPARE(mode->currentText(), u"Updates: Manuell"_s);
}

void UiSmokeTests::cardShowsARunningProjectAndAnUnexpectedEnd()
{
    QVERIFY(writeRegistryCache(m_paths->registryCacheFile(), sampleProjects()));
    m_registry->loadCache();
    QVERIFY(sleepy::install(
        *m_layout, *m_installed, u"lap-timer"_s, u"lap-timer"_s, u"1.4.2"_s, u"lap-timer.exe"_s));
    const QString current = m_layout->currentDir(u"lap-timer"_s);

    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Library);
    QTest::qWait(250);

    ProjectCard* card = nullptr;
    for (ProjectCard* candidate : window.findChildren<ProjectCard*>()) {
        if (candidate->projectId() == u"lap-timer"_s)
            card = candidate;
    }
    QVERIFY(card);
    auto* primary = findIn<QPushButton>(*card, "ProjectPrimary");
    auto* uninstall = findIn<QPushButton>(*card, "ProjectUninstall");
    auto* problem = findIn<QLabel>(*card, "ProjectProblem");
    QCOMPARE(primary->text(), u"Starten"_s);

    // The program ends at once with an error code: the user is told.
    sleepy::writeControlFile(current, u"exitcode"_s, "3");
    sleepy::stop(current);
    primary->click();
    QTRY_VERIFY_WITH_TIMEOUT((m_processes->refresh(), problem->isVisible()), 10000);
    QVERIFY2(problem->text().contains(u"0x3"_s), qPrintable(problem->text()));
    QTRY_COMPARE_WITH_TIMEOUT((m_processes->refresh(), primary->text()), u"Starten"_s, 10000);

    // Starting again clears the old message. This time the program stays alive.
    QFile::remove(current + u"/stop"_s);
    QFile::remove(current + u"/exitcode"_s);
    primary->click();
    QCOMPARE(m_service->lastFailure(u"lap-timer"_s).kind, launcher::OperationFailure::Kind::None);
    QVERIFY(!problem->isVisible());
    QCOMPARE(primary->text(), u"Läuft"_s);
    QVERIFY(!primary->isEnabled());   // no second start
    QVERIFY(!uninstall->isVisible()); // and no removal while it runs
    QVERIFY(findIn<QLabel>(*card, "ProjectState")->text().contains(u"läuft"_s));
    saveScreenshot(window, u"library-running"_s);

    sleepy::stop(current);
    QTRY_COMPARE_WITH_TIMEOUT((m_processes->refresh(), primary->text()), u"Starten"_s, 10000);
    QVERIFY(primary->isEnabled());
    QVERIFY(uninstall->isVisible());
}

// ---- The launcher's own update ----------------------------------------------------------

namespace {

/// A release list with one stable launcher release.
QByteArray launcherReleaseList(const QString& version)
{
    const QJsonObject asset {{u"name"_s, u"KERJAMI-"_s + version + u"-Windows-x64-Portable.zip"_s},
        {u"size"_s, 123456}, {u"state"_s, u"uploaded"_s}, {u"digest"_s, u"sha256:"_s + u"b"_s.repeated(64)}};
    const QJsonObject release {{u"tag_name"_s, u"v"_s + version}, {u"name"_s, u"Release"_s},
        {u"body"_s, u"- Selbstupdate\n- Fehler behoben"_s}, {u"draft"_s, false}, {u"prerelease"_s, false},
        {u"published_at"_s, u"2026-11-01T12:00:00Z"_s}, {u"assets"_s, QJsonArray {asset}}};
    return QJsonDocument(QJsonArray {release}).toJson();
}

} // namespace

void UiSmokeTests::launcherUpdatePanelsFollowTheManager()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Dashboard and settings show the same element.
    auto* dashboard = find<QWidget>(window, "DashboardLauncherUpdate");
    auto* settings = find<QWidget>(window, "SettingsLauncherUpdate");
    QVERIFY(dashboard && settings);
    auto* status = dashboard->findChild<QLabel*>(u"LauncherUpdateStatus"_s);
    auto* check = dashboard->findChild<QPushButton*>(u"LauncherUpdateCheck"_s);
    auto* install = dashboard->findChild<QPushButton*>(u"LauncherUpdateInstall"_s);
    QVERIFY(status && check && install);
    QVERIFY(status->text().contains(u"Noch nicht nach Updates gesucht"_s));
    QVERIFY(status->text().contains(u"1.0.0"_s));

    window.showPage(PageId::Dashboard);
    QTest::qWait(200);
    QVERIFY(check->isVisible());
    QVERIFY(!install->isVisible());
    QVERIFY(!dashboard->findChild<QProgressBar*>(u"LauncherUpdateProgress"_s)->isVisible());

    m_releaseBody = launcherReleaseList(u"2.0.0"_s);
    check->click();
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(u"Version 2.0.0 ist verfügbar"_s), 10000);
    QVERIFY(install->isVisible());
    QCOMPARE(install->text(), u"Aktualisieren"_s);
    QCOMPARE(settings->findChild<QLabel*>(u"LauncherUpdateStatus"_s)->text(), status->text());
    saveScreenshot(window, u"dashboard-launcher-update"_s);

    // Up to date.
    m_releaseBody = launcherReleaseList(u"1.0.0"_s);
    check->click();
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(u"ist aktuell"_s), 10000);
    QVERIFY(!install->isVisible());

    // The look fails (no answer): what is known stays on the screen.
    m_releaseBody = launcherReleaseList(u"2.0.0"_s);
    m_server->close();
    m_launcherUpdates->checkNow();
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(u"Die letzte Suche war nicht möglich"_s), 15000);
}

void UiSmokeTests::launcherUpdateAsksBeforeDownloadingAndCanBeCancelled()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Dashboard);

    auto* panel = find<QWidget>(window, "DashboardLauncherUpdate");
    auto* status = panel->findChild<QLabel*>(u"LauncherUpdateStatus"_s);
    auto* check = panel->findChild<QPushButton*>(u"LauncherUpdateCheck"_s);
    auto* install = panel->findChild<QPushButton*>(u"LauncherUpdateInstall"_s);
    auto* cancel = panel->findChild<QPushButton*>(u"LauncherUpdateCancel"_s);
    auto* progress = panel->findChild<QProgressBar*>(u"LauncherUpdateProgress"_s);

    m_releaseBody = launcherReleaseList(u"2.0.0"_s);
    check->click();
    QTRY_VERIFY_WITH_TIMEOUT(install->isVisible(), 10000);

    // "No": nothing is downloaded.
    QString seenVersion;
    QString seenNotes;
    launcher::ui::Dialogs::setLauncherUpdateConfirmationForTests(
        [&](const launcher::LauncherUpdateInfo& info) {
            seenVersion = info.latestVersion;
            seenNotes = info.notes;
            return false;
        });
    install->click();
    QCOMPARE(seenVersion, u"2.0.0"_s);
    QVERIFY(seenNotes.contains(u"Selbstupdate"_s));
    QCOMPARE(m_launcherUpdates->info().state, launcher::LauncherUpdateState::Available);
    QVERIFY(!cancel->isVisible());

    // "Yes": the update starts and can be cancelled.
    launcher::ui::Dialogs::setLauncherUpdateConfirmationForTests(
        [](const launcher::LauncherUpdateInfo&) { return true; });
    install->click();
    launcher::ui::Dialogs::setLauncherUpdateConfirmationForTests({});
    QCOMPARE(m_launcherUpdates->info().state, launcher::LauncherUpdateState::Downloading);
    QVERIFY(status->text().contains(u"wird geladen"_s));
    QVERIFY(cancel->isVisible());
    QVERIFY(progress->isVisible());
    QVERIFY(!install->isVisible());

    cancel->click();
    QCOMPARE(m_launcherUpdates->info().state, launcher::LauncherUpdateState::Available);
    QVERIFY(install->isVisible());
    QVERIFY(!cancel->isVisible());
    QVERIFY(!progress->isVisible());
}

void UiSmokeTests::launcherUpdateFailuresAreExplained()
{
    MainWindow window(context());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Dashboard);

    auto* panel = find<QWidget>(window, "DashboardLauncherUpdate");
    auto* status = panel->findChild<QLabel*>(u"LauncherUpdateStatus"_s);
    auto* check = panel->findChild<QPushButton*>(u"LauncherUpdateCheck"_s);
    auto* install = panel->findChild<QPushButton*>(u"LauncherUpdateInstall"_s);
    auto* problem = panel->findChild<QLabel*>(u"LauncherUpdateProblem"_s);

    // The test server delivers a file that is not the size the release announced.
    m_releaseBody = launcherReleaseList(u"2.0.0"_s);
    check->click();
    QTRY_VERIFY_WITH_TIMEOUT(install->isVisible(), 10000);
    launcher::ui::Dialogs::setLauncherUpdateConfirmationForTests(
        [](const launcher::LauncherUpdateInfo&) { return true; });
    install->click();
    launcher::ui::Dialogs::setLauncherUpdateConfirmationForTests({});

    QTRY_VERIFY_WITH_TIMEOUT(problem->isVisible(), 15000);
    QVERIFY2(problem->text().contains(u"Größe"_s), qPrintable(problem->text()));
    QVERIFY(status->text().contains(u"fehlgeschlagen"_s));
    QCOMPARE(install->text(), u"Erneut versuchen"_s); // the version is still known
    QVERIFY(install->isVisible());
    saveScreenshot(window, u"dashboard-launcher-update-failed"_s);

    // A new look clears the failure.
    m_server->close();
    m_server.reset();
}

void UiSmokeTests::startupMessagesAreWorded()
{
    using launcher::TransactionOutcome;
    using launcher::TransactionResult;
    using launcher::UpdateReport;
    using Level = launcher::ui::StartupMessage::Level;

    UpdateReport report;
    report.fromVersion = u"1.0.0"_s;
    report.toVersion = u"2.0.0"_s;
    report.backupDir = u"updater/backups/1.0.0-x"_s;

    report.status = UpdateReport::Status::Success;
    auto message = launcher::ui::updateReportMessage(report);
    QVERIFY(message.has_value());
    QCOMPARE(message->level, Level::Information);
    QVERIFY(message->text.contains(u"2.0.0"_s));

    report.status = UpdateReport::Status::RolledBack;
    report.failedPath = u"extra.dll"_s;
    message = launcher::ui::updateReportMessage(report);
    QCOMPARE(message->level, Level::Warning);
    QVERIFY(message->text.contains(u"wiederhergestellt"_s));
    QVERIFY(message->text.contains(u"extra.dll"_s));

    report.status = UpdateReport::Status::NotApplied;
    report.detail = u"the launcher did not close in time"_s;
    message = launcher::ui::updateReportMessage(report);
    QCOMPARE(message->level, Level::Warning);
    QVERIFY(message->text.contains(u"nichts verändert"_s));

    report.status = UpdateReport::Status::Failed;
    message = launcher::ui::updateReportMessage(report);
    QCOMPARE(message->level, Level::Critical);
    QVERIFY(message->text.contains(u"updater"_s) || message->text.contains(u"Ordner"_s));

    TransactionResult result;
    result.outcome = TransactionOutcome::Success;
    QCOMPARE(launcher::ui::recoveryMessage(result)->level, Level::Information);
    result.outcome = TransactionOutcome::RolledBack;
    QCOMPARE(launcher::ui::recoveryMessage(result)->level, Level::Warning);
    result.outcome = TransactionOutcome::RollbackFailed;
    QCOMPARE(launcher::ui::recoveryMessage(result)->level, Level::Critical);
    result.outcome = TransactionOutcome::NothingToDo;
    QVERIFY(!launcher::ui::recoveryMessage(result).has_value());
}

QTEST_MAIN(UiSmokeTests)
#include "UiSmokeTests.moc"
