// Main window: navigation, start page and settings controls, driven the way a
// user would. Runs on the offscreen platform, so no window appears.
//
// If LAUNCHER_TEST_SCREENSHOT_DIR is set (CTest sets it), a screenshot of every
// page is saved there for visual review.

#include "app/Branding.h"
#include "core/PathManager.h"
#include "core/SettingsManager.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QPixmap>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using launcher::PathManager;
using launcher::SettingsManager;
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

private:
    template <typename T> static T* find(QWidget& parent, const char* objectName)
    {
        return parent.findChild<T*>(QLatin1StringView(objectName));
    }

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<PathManager> m_paths;
    std::unique_ptr<SettingsManager> m_settings;
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
}

void UiSmokeTests::cleanup()
{
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
    MainWindow window(*m_paths, *m_settings);
    QCOMPARE(window.currentPage(), PageId::Dashboard);
    QVERIFY(find<QAbstractButton>(window, "NavDashboard")->isChecked());
    QVERIFY(!find<QAbstractButton>(window, "NavLibrary")->isChecked());
}

void UiSmokeTests::startPageSettingIsRespected()
{
    QVERIFY(m_settings->setStartPage(StartPage::Library));

    MainWindow window(*m_paths, *m_settings);
    QCOMPARE(window.currentPage(), PageId::Library);
    QVERIFY(find<QAbstractButton>(window, "NavLibrary")->isChecked());
    QVERIFY(!find<QAbstractButton>(window, "NavDashboard")->isChecked());
}

void UiSmokeTests::sidebarSwitchesPages()
{
    MainWindow window(*m_paths, *m_settings);
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
    MainWindow window(*m_paths, *m_settings);
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
    MainWindow window(*m_paths, *m_settings);
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
    MainWindow window(*m_paths, *m_settings);
    QVERIFY(!find<QLabel>(window, "BrandEmblem")->pixmap().isNull());
    QVERIFY(!find<QLabel>(window, "WelcomeLogo")->pixmap().isNull());
    QVERIFY(!window.windowIcon().isNull());
}

void UiSmokeTests::settingsControlsShowStoredValues()
{
    QVERIFY(m_settings->setRegistryAutoCheck(false));
    QVERIFY(m_settings->setUpdateCheckIntervalMinutes(360));
    QVERIFY(m_settings->setStartPage(StartPage::Library));

    MainWindow window(*m_paths, *m_settings);
    QVERIFY(!find<QAbstractButton>(window, "RegistryAutoCheck")->isChecked());
    QVERIFY(find<QAbstractButton>(window, "LauncherUpdateAutoCheck")->isChecked());
    QCOMPARE(find<QComboBox>(window, "CheckInterval")->currentData().toInt(), 360);
    QCOMPARE(
        find<QComboBox>(window, "StartPage")->currentData().toInt(), static_cast<int>(StartPage::Library));
    QCOMPARE(find<QLabel>(window, "RootPath")->text(), QDir::toNativeSeparators(m_paths->root()));
}

void UiSmokeTests::settingsControlsPersistChanges()
{
    MainWindow window(*m_paths, *m_settings);
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
    MainWindow window(*m_paths, *m_settings);
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

QTEST_MAIN(UiSmokeTests)
#include "UiSmokeTests.moc"
