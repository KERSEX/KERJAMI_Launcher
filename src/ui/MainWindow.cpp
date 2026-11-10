#include "ui/MainWindow.h"

#include "app/Branding.h"
#include "core/SettingsManager.h"
#include "ui/Controls.h"
#include "ui/DashboardPage.h"
#include "ui/DownloadsPage.h"
#include "ui/LibraryPage.h"
#include "ui/SettingsPage.h"
#include "ui/Theme.h"

#include <QButtonGroup>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace launcher::ui {

namespace {

constexpr int kSidebarWidth = 232;
constexpr int kFadeDurationMs = 140;

} // namespace

MainWindow::MainWindow(const AppContext& context, QWidget* parent)
    : QMainWindow(parent)
{
    SettingsManager& settings = context.settings;
    setWindowTitle(branding::displayName());
    setWindowIcon(Theme::applicationIcon());
    setMinimumSize(960, 600);
    resize(1240, 780);

    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_pages = new QStackedWidget(central);

    // Order must match PageId.
    auto* dashboard = new DashboardPage(m_pages);
    m_pages->addWidget(dashboard);
    m_pages->addWidget(new LibraryPage(context, m_pages));
    m_pages->addWidget(new DownloadsPage(context.downloads, m_pages));
    m_pages->addWidget(new SettingsPage(context.paths, settings, m_pages));

    layout->addWidget(createSidebar());
    layout->addWidget(m_pages, 1);
    setCentralWidget(central);

    connect(dashboard, &DashboardPage::libraryRequested, this, [this] { showPage(PageId::Library); });

    connect(&settings, &SettingsManager::saveFailed, this, [this](const QString& reason) {
        QMessageBox::warning(this, tr("Einstellungen"),
            tr("Die Einstellungen konnten nicht gespeichert werden.\n\n%1").arg(reason));
    });

    const bool startInLibrary = settings.settings().startPage == StartPage::Library;
    switchTo(startInLibrary ? PageId::Library : PageId::Dashboard, false);
}

MainWindow::PageId MainWindow::currentPage() const
{
    return static_cast<PageId>(m_pages->currentIndex());
}

void MainWindow::showPage(PageId page)
{
    switchTo(page, true);
}

QWidget* MainWindow::createSidebar()
{
    auto* sidebar = new QFrame(this);
    sidebar->setObjectName("Sidebar");
    sidebar->setFixedWidth(kSidebarWidth);

    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(14, 22, 14, 16);
    layout->setSpacing(4);

    auto* brand = new QHBoxLayout;
    brand->setContentsMargins(8, 0, 0, 0);
    brand->setSpacing(10);
    auto* brandEmblem = new QLabel(sidebar);
    brandEmblem->setObjectName("BrandEmblem");
    brandEmblem->setPixmap(Theme::brandImage(u"emblem"_s, 26, devicePixelRatioF()));
    brand->addWidget(brandEmblem);
    auto* brandName = new QLabel(branding::productName(), sidebar);
    brandName->setObjectName("BrandName");
    brand->addWidget(brandName, 1);
    layout->addLayout(brand);
    layout->addSpacing(22);

    m_navigation = new QButtonGroup(this);
    m_navigation->setExclusive(true);

    const auto addEntry = [&](PageId page, char16_t glyph, const QString& text, const char* objectName) {
        auto* button = new NavButton(glyph, text, sidebar);
        button->setObjectName(objectName);
        m_navigation->addButton(button, static_cast<int>(page));
        layout->addWidget(button);
    };
    addEntry(PageId::Dashboard, Glyph::Home, tr("Dashboard"), "NavDashboard");
    addEntry(PageId::Library, Glyph::Library, tr("Bibliothek"), "NavLibrary");
    addEntry(PageId::Downloads, Glyph::Download, tr("Downloads"), "NavDownloads");
    layout->addStretch(1);
    addEntry(PageId::Settings, Glyph::Settings, tr("Einstellungen"), "NavSettings");

    layout->addSpacing(10);
    auto* version = new QLabel(tr("Version %1").arg(branding::version()), sidebar);
    version->setObjectName("SidebarVersion");
    version->setContentsMargins(8, 0, 0, 0);
    layout->addWidget(version);

    connect(
        m_navigation, &QButtonGroup::idClicked, this, [this](int id) { showPage(static_cast<PageId>(id)); });

    return sidebar;
}

void MainWindow::switchTo(PageId page, bool animate)
{
    if (QAbstractButton* button = m_navigation->button(static_cast<int>(page)))
        button->setChecked(true);

    QWidget* target = m_pages->widget(static_cast<int>(page));
    if (!target || m_pages->currentWidget() == target)
        return;

    m_pages->setCurrentWidget(target);
    if (animate)
        fadeIn(target);
}

void MainWindow::fadeIn(QWidget* page)
{
    // Installing a new effect deletes one that is still running on this page.
    auto* effect = new QGraphicsOpacityEffect(page);
    effect->setOpacity(0.0);
    page->setGraphicsEffect(effect);

    auto* animation = new QPropertyAnimation(effect, "opacity", page);
    animation->setDuration(kFadeDurationMs);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QPropertyAnimation::finished, page, [page, effect] {
        // The effect renders the page through an offscreen buffer on every
        // repaint, so it is removed as soon as the transition is over.
        if (page->graphicsEffect() == effect)
            page->setGraphicsEffect(nullptr);
    });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace launcher::ui
