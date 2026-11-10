#include "updater/ProjectUpdateManager.h"

#include "core/Logger.h"
#include "core/SemVer.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

constexpr int kFirstCheckDelayMs = 3000;

} // namespace

ProjectUpdateManager::ProjectUpdateManager(GitHubClient& client, RegistryManager& registry,
    ReleaseManager& releases, InstallService& installService, ProcessManager& processes,
    InstalledProjects& installed, SettingsManager& settings, QObject* parent)
    : QObject(parent)
    , m_client(client)
    , m_registry(registry)
    , m_releases(releases)
    , m_service(installService)
    , m_processes(processes)
    , m_installed(installed)
    , m_settings(settings)
{
    connect(&m_timer, &QTimer::timeout, this, &ProjectUpdateManager::checkAll);

    connect(&m_releases, &ReleaseManager::releaseReady, this, &ProjectUpdateManager::onReleaseReady);
    connect(&m_releases, &ReleaseManager::fetchFailed, this, &ProjectUpdateManager::onReleaseFailed);

    connect(&m_service, &InstallService::statusChanged, this, &ProjectUpdateManager::onServiceStatus);
    connect(&m_service, &InstallService::prepared, this, &ProjectUpdateManager::onPrepared);
    connect(&m_service, &InstallService::installFinished, this, &ProjectUpdateManager::onInstallFinished);

    connect(&m_processes, &ProcessManager::runningChanged, this, &ProjectUpdateManager::onRunningChanged);
    connect(&m_installed, &InstalledProjects::changed, this, &ProjectUpdateManager::onInstalledChanged);
    connect(&m_settings, &SettingsManager::projectSettingsChanged, this,
        &ProjectUpdateManager::onProjectSettingsChanged);
    connect(
        &m_settings, &SettingsManager::settingsChanged, this, &ProjectUpdateManager::applyIntervalSetting);
}

void ProjectUpdateManager::start()
{
    applyIntervalSetting();
    m_timer.start();
    QTimer::singleShot(kFirstCheckDelayMs, this, &ProjectUpdateManager::checkAll);
}

void ProjectUpdateManager::applyIntervalSetting()
{
    const int minutes = m_settings.settings().updateCheckIntervalMinutes;
    m_timer.setInterval(minutes * 60 * 1000);
}

UpdateInfo ProjectUpdateManager::info(const QString& projectId) const
{
    UpdateInfo info = m_info.value(projectId);
    if (const auto record = m_installed.get(projectId))
        info.installedVersion = record->version;
    return info;
}

std::optional<Project> ProjectUpdateManager::registryProject(const QString& projectId) const
{
    for (const Project& project : m_registry.projects()) {
        if (project.id == projectId)
            return project;
    }
    return std::nullopt;
}

void ProjectUpdateManager::setState(const QString& projectId, UpdateState state)
{
    UpdateInfo& info = m_info[projectId];
    if (info.state == state)
        return;
    info.state = state;
    emit updateChanged(projectId);
}

void ProjectUpdateManager::refreshInstalledVersion(const QString& projectId)
{
    UpdateInfo& info = m_info[projectId];
    const auto record = m_installed.get(projectId);
    info.installedVersion = record ? record->version : QString();
}

// ---- checking ----------------------------------------------------------------------------

void ProjectUpdateManager::checkAll()
{
    if (!m_checking.isEmpty() || !m_checkQueue.isEmpty())
        return; // a round is under way

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QStringList ids;
    for (const InstalledProject& installed : m_installed.all()) {
        const QString& id = installed.projectId;
        if (!registryProject(id))
            continue; // not (or no longer) approved: nothing to ask
        if (m_active.contains(id) || m_queue.contains(id) || m_service.isBusy(id))
            continue;
        const QDateTime asked = m_lastAsked.value(id);
        if (asked.isValid() && asked.secsTo(now) < m_minCheckSpacing)
            continue;
        ids.append(id);
    }

    if (ids.isEmpty()) {
        emit checkFinished();
        return;
    }

    // One request per project. If the budget is nearly used up, installing a
    // project would fail for lack of requests, so checks wait for the next round.
    const RateLimit limit = m_client.rateLimit();
    if (limit.isKnown() && limit.remaining < kRateLimitReserve + ids.size()) {
        qCInfo(lcUpdate) << "Skipping the update check:" << limit.remaining << "requests left";
        emit checkFinished();
        return;
    }

    qCInfo(lcUpdate) << "Checking" << ids.size() << "project(s) for updates";
    m_checkQueue = ids;
    nextCheck();
}

void ProjectUpdateManager::nextCheck()
{
    while (!m_checkQueue.isEmpty()) {
        const QString id = m_checkQueue.takeFirst();
        const auto project = registryProject(id);
        if (!project || !m_installed.contains(id))
            continue;
        m_checking = id;
        m_lastAsked.insert(id, QDateTime::currentDateTimeUtc());
        m_releases.fetchLatest(*project);
        return;
    }
    m_checking.clear();
    emit checkFinished();
}

void ProjectUpdateManager::onReleaseReady(const QString& projectId, const ReleaseSelection& selection)
{
    // Every answer about an installed project is useful, also one asked for by
    // somebody else (the user pressed a button); only our own ends a step.
    if (m_installed.contains(projectId))
        evaluate(projectId, selection);
    if (projectId == m_checking) {
        m_checking.clear();
        nextCheck();
    }
}

void ProjectUpdateManager::onReleaseFailed(const QString& projectId, ReleaseManager::Problem problem)
{
    if (projectId != m_checking)
        return;
    m_checking.clear();

    // A failed check says nothing about the project: the last known state stays,
    // so that a dropped connection neither announces nor withdraws an update.
    UpdateInfo& info = m_info[projectId];
    info.lastCheckFailed = true;
    emit updateChanged(projectId);

    using Problem = ReleaseManager::Problem;
    if (problem == Problem::Unreachable || problem == Problem::RateLimited) {
        // No point in asking for the others now.
        qCInfo(lcUpdate) << "Update check stopped:" << static_cast<int>(problem);
        m_checkQueue.clear();
    }
    nextCheck();
}

void ProjectUpdateManager::evaluate(const QString& projectId, const ReleaseSelection& selection)
{
    const auto record = m_installed.get(projectId);
    if (!record)
        return;

    UpdateInfo& info = m_info[projectId];
    info.installedVersion = record->version;
    info.latestVersion = selection.release.version.toString();
    info.tag = selection.release.tag;
    info.notes = selection.release.notes;
    info.publishedAt = selection.release.publishedAt;
    info.assetSize = selection.asset.size;
    info.checkedAt = QDateTime::currentDateTimeUtc();
    info.lastCheckFailed = false;

    // An update that is under way keeps its state; only its data was refreshed.
    if (m_active.contains(projectId) || m_queue.contains(projectId)) {
        emit updateChanged(projectId);
        return;
    }

    const std::optional<SemVer> installedVersion = SemVer::parse(record->version);
    const bool newer = installedVersion && selection.release.version > *installedVersion;
    if (!newer) {
        // Equal or older: never a downgrade.
        info.failure = {};
        m_noAutoVersion.remove(projectId);
        if (info.state == UpdateState::UpToDate)
            emit updateChanged(projectId);
        else
            setState(projectId, UpdateState::UpToDate);
        return;
    }

    // A version that failed or was cancelled stays as it is until the user acts.
    const bool settledOnThisVersion = m_noAutoVersion.value(projectId) == info.latestVersion;
    if (info.state == UpdateState::Failed && settledOnThisVersion) {
        emit updateChanged(projectId);
        return;
    }
    if (info.state == UpdateState::Failed)
        info.failure = {}; // a newer version than the failed one: start over
    if (info.state == UpdateState::Available)
        emit updateChanged(projectId); // the data may have changed
    else
        setState(projectId, UpdateState::Available);

    if (m_settings.updateMode(projectId) == UpdateMode::Automatic && !settledOnThisVersion)
        enqueue(projectId, true);
}

// ---- running an update ------------------------------------------------------------------

void ProjectUpdateManager::applyUpdate(const QString& projectId)
{
    const UpdateInfo current = info(projectId);
    if (current.state != UpdateState::Available && current.state != UpdateState::Failed)
        return;
    m_noAutoVersion.remove(projectId);
    enqueue(projectId, false);
}

void ProjectUpdateManager::enqueue(const QString& projectId, bool automatic)
{
    // Only an installed project is ever updated, and only one that is still approved.
    if (!m_installed.contains(projectId) || !registryProject(projectId))
        return;
    if (m_queue.contains(projectId) || m_active.contains(projectId))
        return;

    UpdateInfo& info = m_info[projectId];
    info.automatic = automatic;
    info.failure = {};
    m_queue.append(projectId);
    qCInfo(lcUpdate).noquote() << "Update of" << projectId << "to" << info.latestVersion << "queued"
                               << (automatic ? "(automatic)" : "(by the user)");
    setState(projectId, UpdateState::Queued);
    pump();
}

void ProjectUpdateManager::pump()
{
    while (m_active.size() < kMaxParallelUpdates && !m_queue.isEmpty()) {
        const QString id = m_queue.takeFirst();
        const auto project = registryProject(id);
        if (!project || !m_installed.contains(id) || m_service.isBusy(id)) {
            // Gone from the registry, uninstalled, or the user is working on it: not now.
            setState(id, UpdateState::Available);
            continue;
        }
        m_active.insert(id);
        setState(id, UpdateState::Downloading);
        m_service.install(*project, InstallService::Mode::PrepareOnly);
    }
}

void ProjectUpdateManager::onServiceStatus(const QString& projectId)
{
    if (!m_active.contains(projectId))
        return;

    using Stage = ProjectStatus::Stage;
    switch (m_service.status(projectId).stage) {
    case Stage::FetchingRelease:
    case Stage::FetchingChecksum:
    case Stage::Downloading:
        setState(projectId, UpdateState::Downloading);
        break;
    case Stage::Verifying:
        setState(projectId,
            m_installingNow.contains(projectId) ? UpdateState::Installing : UpdateState::Verifying);
        break;
    case Stage::Extracting:
    case Stage::Replacing:
        setState(projectId, UpdateState::Installing);
        break;
    case Stage::Prepared: // reported by prepared(), which decides what happens next
    case Stage::Idle:     // reported by installFinished()
        break;
    }
}

void ProjectUpdateManager::onPrepared(const QString& projectId)
{
    if (!m_active.contains(projectId))
        return;
    if (m_processes.isRunningNow(projectId)) {
        qCInfo(lcUpdate).noquote() << "Update of" << projectId << "is pending: the project is running";
        setState(projectId, UpdateState::Pending);
        return;
    }
    m_installingNow.insert(projectId);
    setState(projectId, UpdateState::Installing);
    m_service.installPrepared(projectId);
}

void ProjectUpdateManager::onRunningChanged(const QString& projectId, bool running)
{
    if (!running)
        tryInstallPending(projectId);
}

void ProjectUpdateManager::tryInstallPending(const QString& projectId)
{
    if (!m_active.contains(projectId) || m_info.value(projectId).state != UpdateState::Pending)
        return;
    // Looked up again: "not running" must be true now, not merely as of the last look.
    if (m_processes.isRunningNow(projectId))
        return;
    qCInfo(lcUpdate).noquote() << "The project" << projectId << "has ended; installing the pending update";
    m_installingNow.insert(projectId);
    setState(projectId, UpdateState::Installing);
    m_service.installPrepared(projectId);
}

void ProjectUpdateManager::onInstallFinished(const QString& projectId, bool success)
{
    if (!m_active.contains(projectId))
        return;
    m_active.remove(projectId);
    m_installingNow.remove(projectId);

    UpdateInfo& info = m_info[projectId];
    if (success) {
        refreshInstalledVersion(projectId);
        info.failure = {};
        info.automatic = false;
        m_noAutoVersion.remove(projectId);
        setState(projectId, UpdateState::UpToDate);
        emit updateChanged(projectId);
    } else {
        const OperationFailure failure = m_service.lastFailure(projectId);
        if (failure.isFailure()) {
            info.failure = failure;
            // "The project runs" is not the release's fault: the next round may try again.
            const bool projectRunning = failure.kind == OperationFailure::Kind::Install
                && failure.code == static_cast<int>(InstallError::ProjectRunning);
            if (!projectRunning)
                m_noAutoVersion.insert(projectId, info.latestVersion);
            setState(projectId, UpdateState::Failed);
        } else {
            // Cancelled: the update stays announced, but automatic mode does not start it again by itself.
            m_noAutoVersion.insert(projectId, info.latestVersion);
            setState(projectId, UpdateState::Available);
        }
        emit updateChanged(projectId);
    }
    pump();
}

void ProjectUpdateManager::cancel(const QString& projectId)
{
    if (m_queue.removeAll(projectId) > 0) {
        m_noAutoVersion.insert(projectId, m_info.value(projectId).latestVersion);
        setState(projectId, UpdateState::Available);
        return;
    }
    if (m_active.contains(projectId) && !m_installingNow.contains(projectId))
        m_service.cancel(projectId); // reports back through installFinished()
}

// ---- changes elsewhere -----------------------------------------------------------------

void ProjectUpdateManager::onInstalledChanged(const QString& projectId)
{
    const auto record = m_installed.get(projectId);
    if (!record) {
        // Uninstalled: nothing is kept for the project, neither state nor settings.
        m_queue.removeAll(projectId);
        m_active.remove(projectId);
        m_installingNow.remove(projectId);
        m_noAutoVersion.remove(projectId);
        m_lastAsked.remove(projectId);
        m_info.remove(projectId);
        m_settings.removeProjectSettings(projectId);
        emit updateChanged(projectId);
        return;
    }

    UpdateInfo& info = m_info[projectId];
    info.installedVersion = record->version;
    if (m_active.contains(projectId) || m_queue.contains(projectId) || info.latestVersion.isEmpty()) {
        emit updateChanged(projectId);
        return;
    }

    // Installed or replaced by the user: compare with what is known about the newest version.
    const std::optional<SemVer> latest = SemVer::parse(info.latestVersion);
    const std::optional<SemVer> installed = SemVer::parse(record->version);
    const bool newer = latest && installed && *latest > *installed;
    if (newer) {
        if (info.state != UpdateState::Failed)
            setState(projectId, UpdateState::Available);
    } else {
        info.failure = {};
        setState(projectId, UpdateState::UpToDate);
    }
    emit updateChanged(projectId);
}

void ProjectUpdateManager::onProjectSettingsChanged(const QString& projectId)
{
    if (!m_installed.contains(projectId))
        return; // settings of a project that is not installed have no effect
    const UpdateMode mode = m_settings.updateMode(projectId);
    UpdateInfo& info = m_info[projectId];

    if (mode == UpdateMode::Automatic) {
        // The user switched automatic updates on: a known update is wanted now.
        if (info.state == UpdateState::Available) {
            m_noAutoVersion.remove(projectId);
            enqueue(projectId, true);
        }
        return;
    }

    // Back to manual: an automatic update that has not started installing is withdrawn.
    if (info.automatic
        && (m_queue.contains(projectId)
            || (m_active.contains(projectId) && !m_installingNow.contains(projectId))))
        cancel(projectId);
}

} // namespace launcher
