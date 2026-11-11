#pragma once

namespace launcher {

/// Available colour themes. Version 1 ships the dark theme only.
enum class ThemeId {
    Dark,
};

/// Page shown when the launcher starts.
enum class StartPage {
    Dashboard,
    Library,
};

/// Preferences of the launcher itself, persisted in launcher/settings.json.
/// Per-project settings are stored in the same file but handled separately.
struct LauncherSettings {
    /// Lower bound protects the unauthenticated GitHub rate limit (60 requests per hour).
    static constexpr int kMinCheckIntervalMinutes = 15;
    static constexpr int kMaxCheckIntervalMinutes = 24 * 60;

    ThemeId theme = ThemeId::Dark;
    StartPage startPage = StartPage::Dashboard;

    /// Look for new launcher versions automatically. Installing always needs confirmation.
    bool launcherUpdateAutoCheck = true;

    /// Minutes between automatic checks for project updates.
    int updateCheckIntervalMinutes = 60;

    bool operator==(const LauncherSettings&) const = default;
};

} // namespace launcher
