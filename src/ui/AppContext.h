#pragma once

namespace launcher {
class DownloadManager;
class InstallService;
class InstalledProjects;
class PathManager;
class ProcessManager;
class ProjectUpdateManager;
class RegistryManager;
class SettingsManager;
} // namespace launcher

namespace launcher::ui {

/// Everything the pages show and call, in one place. The window and the pages
/// take references to the managers; they own none of them and create none.
struct AppContext {
    const PathManager& paths;
    SettingsManager& settings;
    RegistryManager& registry;
    DownloadManager& downloads;
    InstalledProjects& installed;
    InstallService& installService;
    ProcessManager& processes;
    ProjectUpdateManager& updates;
};

} // namespace launcher::ui
