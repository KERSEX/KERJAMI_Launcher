// Main window: navigation, start page and settings controls, driven the way a
// user would. Runs on the offscreen platform, so no window appears.
//
// If LAUNCHER_TEST_SCREENSHOT_DIR is set (CTest sets it), a screenshot of every
// page is saved there for visual review.

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
#include "ui/Dialogs.h"
#include "ui/MainWindow.h"
#include "ui/ProjectCard.h"
#include "ui/Theme.h"

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

    void downloadsPageStartsEmpty();
    void downloadsPageShowsProgressAndCancels();
    void downloadsPageShowsFinishedAndFailedDownloads();
    void downloadsPageClearsTheHistory();

private:
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
    m_server = std::make_unique<TestHttpServer>([](const TestHttpServer::Request& request) {
        TestHttpServer::Response response;
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
}

void UiSmokeTests::cleanup()
{
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
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
    QCOMPARE(window.currentPage(), PageId::Dashboard);
    QVERIFY(find<QAbstractButton>(window, "NavDashboard")->isChecked());
    QVERIFY(!find<QAbstractButton>(window, "NavLibrary")->isChecked());
}

void UiSmokeTests::startPageSettingIsRespected()
{
    QVERIFY(m_settings->setStartPage(StartPage::Library));

    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
    QCOMPARE(window.currentPage(), PageId::Library);
    QVERIFY(find<QAbstractButton>(window, "NavLibrary")->isChecked());
    QVERIFY(!find<QAbstractButton>(window, "NavDashboard")->isChecked());
}

void UiSmokeTests::sidebarSwitchesPages()
{
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
    QVERIFY(!find<QLabel>(window, "BrandEmblem")->pixmap().isNull());
    QVERIFY(!find<QLabel>(window, "WelcomeLogo")->pixmap().isNull());
    QVERIFY(!window.windowIcon().isNull());
}

void UiSmokeTests::settingsControlsShowStoredValues()
{
    QVERIFY(m_settings->setRegistryAutoCheck(false));
    QVERIFY(m_settings->setUpdateCheckIntervalMinutes(360));
    QVERIFY(m_settings->setStartPage(StartPage::Library));

    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
    QVERIFY(!find<QAbstractButton>(window, "RegistryAutoCheck")->isChecked());
    QVERIFY(find<QAbstractButton>(window, "LauncherUpdateAutoCheck")->isChecked());
    QCOMPARE(find<QComboBox>(window, "CheckInterval")->currentData().toInt(), 360);
    QCOMPARE(
        find<QComboBox>(window, "StartPage")->currentData().toInt(), static_cast<int>(StartPage::Library));
    QCOMPARE(find<QLabel>(window, "RootPath")->text(), QDir::toNativeSeparators(m_paths->root()));
}

void UiSmokeTests::settingsControlsPersistChanges()
{
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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

    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.showPage(PageId::Downloads);

    QVERIFY(find<QWidget>(window, "DownloadsEmpty")->isVisibleTo(&window));
    QVERIFY(window.findChildren<QFrame*>(u"DownloadRow"_s).isEmpty());
}

void UiSmokeTests::downloadsPageShowsProgressAndCancels()
{
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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
    QVERIFY(bar->value() > 200 && bar->value() < 500); // about a third of the file
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
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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
    QVERIFY(window.findChildren<QProgressBar*>().isEmpty());
    QVERIFY(window.findChildren<QAbstractButton*>(u"DownloadCancel"_s).isEmpty());
    saveScreenshot(window, u"downloads-history"_s);
}

void UiSmokeTests::downloadsPageClearsTheHistory()
{
    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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

    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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

    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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

    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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

    MainWindow window(*m_paths, *m_settings, *m_registry, *m_downloads, *m_service, *m_installed);
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

QTEST_MAIN(UiSmokeTests)
#include "UiSmokeTests.moc"
