#pragma once

#include "models/LauncherSettings.h"

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPixmap>

class QApplication;

namespace launcher::ui {

/// Colours of the design system. Widgets that paint themselves read these;
/// everything else is styled through resources/styles/app.qss.
struct ThemePalette {
    QColor background;   ///< Main window background.
    QColor surface;      ///< Sidebar, input fields, dialogs.
    QColor card;         ///< Cards.
    QColor cardHover;    ///< Raised controls on cards, hovered rows.
    QColor border;       ///< Hairlines and card outlines.
    QColor borderStrong; ///< Hovered outlines, inactive switch track.
    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor danger; ///< Error text.
    QColor textPrimary;
    QColor textSecondary;
    QColor textOnAccent;
};

/// Code points in the Windows icon fonts (Segoe Fluent Icons, Segoe MDL2 Assets).
/// Both fonts share these values.
namespace Glyph {
inline constexpr char16_t Home = 0xE80F;
inline constexpr char16_t Library = 0xE8F1;
inline constexpr char16_t Download = 0xE896;
inline constexpr char16_t Settings = 0xE713;
inline constexpr char16_t Warning = 0xE7BA;
}

namespace Theme {

/// Applies style, fonts, palette and style sheet to the whole application.
void apply(QApplication& app, ThemeId theme);

/// Colours of the theme set by the last apply() call.
const ThemePalette& palette();

/// False on systems without a Windows icon font; glyphs are then left out.
bool hasIconFont();

/// Font for drawing Glyph code points.
QFont iconFont(int pixelSize);

/// Window and taskbar icon, assembled from the app-<size>.png resources.
QIcon applicationIcon();

/// Brand image from the resources, scaled to @p height device-independent
/// pixels and rendered for @p devicePixelRatio so it stays sharp on high-DPI
/// screens. @p name is "emblem" (logo symbol) or "logo" (symbol with
/// lettering); both are transparent and made for dark surfaces.
/// Returns a null pixmap if the resource is missing.
QPixmap brandImage(const QString& name, int height, qreal devicePixelRatio);

} // namespace Theme

} // namespace launcher::ui
