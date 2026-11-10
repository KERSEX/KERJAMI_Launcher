#include "ui/Theme.h"

#include "core/Logger.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace launcher::ui {

namespace {

ThemePalette g_palette;

ThemePalette darkPalette()
{
    ThemePalette dark;
    dark.background = QColor(0x12, 0x12, 0x14);
    dark.surface = QColor(0x1A, 0x1A, 0x1F);
    dark.card = QColor(0x20, 0x20, 0x27);
    dark.cardHover = QColor(0x28, 0x28, 0x31);
    dark.border = QColor(0x2B, 0x2B, 0x35);
    dark.borderStrong = QColor(0x3F, 0x3F, 0x4C);
    dark.accent = QColor(0x00, 0xC8, 0xFF);
    dark.accentHover = QColor(0x3D, 0xD6, 0xFF);
    dark.accentPressed = QColor(0x00, 0xA4, 0xD1);
    dark.danger = QColor(0xFF, 0x6B, 0x6B);
    dark.textPrimary = QColor(0xF2, 0xF2, 0xF2);
    dark.textSecondary = QColor(0x99, 0x99, 0xA3);
    dark.textOnAccent = QColor(0x05, 0x13, 0x18);
    return dark;
}

ThemePalette paletteFor(ThemeId theme)
{
    switch (theme) {
    case ThemeId::Dark:
        return darkPalette();
    }
    return darkPalette();
}

/// Colours for everything Qt draws without the style sheet (dialogs, menus).
QPalette qtPalette(const ThemePalette& c)
{
    QPalette palette;
    palette.setColor(QPalette::Window, c.background);
    palette.setColor(QPalette::WindowText, c.textPrimary);
    palette.setColor(QPalette::Base, c.surface);
    palette.setColor(QPalette::AlternateBase, c.card);
    palette.setColor(QPalette::Text, c.textPrimary);
    palette.setColor(QPalette::PlaceholderText, c.textSecondary);
    palette.setColor(QPalette::Button, c.card);
    palette.setColor(QPalette::ButtonText, c.textPrimary);
    palette.setColor(QPalette::BrightText, c.textPrimary);
    palette.setColor(QPalette::ToolTipBase, c.card);
    palette.setColor(QPalette::ToolTipText, c.textPrimary);
    palette.setColor(QPalette::Highlight, c.accent);
    palette.setColor(QPalette::HighlightedText, c.textOnAccent);
    palette.setColor(QPalette::Link, c.accent);
    palette.setColor(QPalette::Light, c.borderStrong);
    palette.setColor(QPalette::Midlight, c.border);
    palette.setColor(QPalette::Mid, c.border);
    palette.setColor(QPalette::Dark, c.surface);
    palette.setColor(QPalette::Shadow, c.background);

    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, c.textSecondary);
    return palette;
}

QString rgba(const QColor& color, int alphaPercent)
{
    return u"rgba(%1, %2, %3, %4%)"_s.arg(color.red()).arg(color.green()).arg(color.blue()).arg(alphaPercent);
}

QString loadStyleSheet(const ThemePalette& c)
{
    QFile file(u":/styles/app.qss"_s);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcApp) << "Style sheet resource is missing";
        return {};
    }
    QString sheet = QString::fromUtf8(file.readAll());

    QList<std::pair<QString, QString>> tokens = {
        {u"@background"_s, c.background.name()},
        {u"@surface"_s, c.surface.name()},
        {u"@cardHover"_s, c.cardHover.name()},
        {u"@card"_s, c.card.name()},
        {u"@borderStrong"_s, c.borderStrong.name()},
        {u"@border"_s, c.border.name()},
        {u"@accentHover"_s, c.accentHover.name()},
        {u"@accentPressed"_s, c.accentPressed.name()},
        {u"@danger"_s, c.danger.name()},
        {u"@accentSoft"_s, rgba(c.accent, 16)},
        {u"@accent"_s, c.accent.name()},
        {u"@textPrimary"_s, c.textPrimary.name()},
        {u"@textSecondary"_s, c.textSecondary.name()},
        {u"@textOnAccent"_s, c.textOnAccent.name()},
    };
    // Longest names first, otherwise "@card" would eat the start of "@cardHover".
    std::sort(tokens.begin(), tokens.end(),
        [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    for (const auto& [name, value] : std::as_const(tokens))
        sheet.replace(name, value);
    return sheet;
}

QString iconFontFamily()
{
    static const QString family = [] {
        // Fluent Icons ships with Windows 11, MDL2 Assets with Windows 10 and 11.
        for (const QString& candidate : {u"Segoe Fluent Icons"_s, u"Segoe MDL2 Assets"_s}) {
            if (QFontDatabase::hasFamily(candidate))
                return candidate;
        }
        return QString();
    }();
    return family;
}

} // namespace

void Theme::apply(QApplication& app, ThemeId theme)
{
    g_palette = paletteFor(theme);

    // Fusion renders identically on Windows 10 and 11 and combines cleanly
    // with style sheets, unlike the native Windows styles.
    app.setStyle(QStyleFactory::create(u"Fusion"_s));

    QFont font(u"Segoe UI"_s);
    font.setPointSizeF(10.0);
    app.setFont(font);

    app.setPalette(qtPalette(g_palette));
    app.setStyleSheet(loadStyleSheet(g_palette));
}

const ThemePalette& Theme::palette()
{
    return g_palette;
}

bool Theme::hasIconFont()
{
    return !iconFontFamily().isEmpty();
}

QFont Theme::iconFont(int pixelSize)
{
    QFont font(iconFontFamily());
    font.setPixelSize(pixelSize);
    return font;
}

QIcon Theme::applicationIcon()
{
    // Sizes generated by scripts/make-icons.ps1. Each size is drawn for itself,
    // so small icons stay crisp instead of being scaled down from one image.
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256})
        icon.addFile(u":/images/app-%1.png"_s.arg(size), QSize(size, size));
    return icon;
}

QPixmap Theme::brandImage(const QString& name, int height, qreal devicePixelRatio)
{
    const QPixmap source(u":/images/"_s + name + u".png"_s);
    if (source.isNull()) {
        qCWarning(lcApp) << "Brand image is missing:" << name;
        return {};
    }

    QPixmap scaled = source.scaledToHeight(qRound(height * devicePixelRatio), Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(devicePixelRatio);
    return scaled;
}

} // namespace launcher::ui
