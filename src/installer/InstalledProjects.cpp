#include "installer/InstalledProjects.h"

#include "core/JsonFile.h"
#include "core/Logger.h"
#include "core/SemVer.h"
#include "github/RegistryParser.h"

#include <QJsonValue>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

constexpr auto kVersionKey = "schema_version"_L1;
constexpr auto kProjectsKey = "projects"_L1;

constexpr auto kVersion = "version"_L1;
constexpr auto kTag = "tag"_L1;
constexpr auto kInstallDirectory = "install_directory"_L1;
constexpr auto kExecutable = "executable"_L1;
constexpr auto kInstalledAt = "installed_at"_L1;
constexpr auto kLastStartedAt = "last_started_at"_L1;

QString timeToString(const QDateTime& time)
{
    return time.isValid() ? time.toUTC().toString(Qt::ISODate) : QString();
}

QDateTime timeFromString(const QString& text)
{
    const QDateTime time = QDateTime::fromString(text, Qt::ISODate);
    return time.isValid() ? time.toUTC() : QDateTime();
}

QJsonObject toJson(const InstalledProject& project)
{
    QJsonObject object;
    object.insert(kVersion, project.version);
    object.insert(kTag, project.tag);
    object.insert(kInstallDirectory, project.installDirectory);
    object.insert(kExecutable, project.executable);
    object.insert(kInstalledAt, timeToString(project.installedAt));
    if (project.lastStartedAt.isValid())
        object.insert(kLastStartedAt, timeToString(project.lastStartedAt));
    return object;
}

/// Everything in the file is checked like input from outside: the folder is
/// the user's, but a hand-edited or damaged file must not make the launcher
/// delete or start something outside the library.
std::optional<InstalledProject> fromJson(const QString& id, const QJsonValue& value)
{
    if (!RegistryParser::isValidProjectId(id) || !value.isObject())
        return std::nullopt;
    const QJsonObject object = value.toObject();

    InstalledProject project;
    project.projectId = id;
    project.version = object.value(kVersion).toString();
    project.tag = object.value(kTag).toString();
    project.installDirectory = object.value(kInstallDirectory).toString();
    project.executable = object.value(kExecutable).toString();
    project.installedAt = timeFromString(object.value(kInstalledAt).toString());
    project.lastStartedAt = timeFromString(object.value(kLastStartedAt).toString());

    if (!SemVer::parse(project.version).has_value())
        return std::nullopt;
    if (!RegistryParser::isValidInstallDirectory(project.installDirectory)
        || !RegistryParser::isValidExecutablePath(project.executable))
        return std::nullopt;
    if (project.tag.size() > 128)
        project.tag.truncate(128);
    return project;
}

} // namespace

InstalledProjects::InstalledProjects(const QString& filePath, QObject* parent)
    : QObject(parent)
    , m_filePath(filePath)
{
}

InstalledProjects::LoadResult InstalledProjects::load()
{
    QMutexLocker lock(&m_mutex);
    m_projects.clear();
    m_foreign = {};
    m_backupPath.clear();

    const JsonFile::LoadResult file = JsonFile::load(m_filePath);
    if (file.status == JsonFile::Status::NotFound)
        return LoadResult::CreatedEmpty;

    if (file.status != JsonFile::Status::Ok) {
        qCWarning(lcInstall).noquote() << "Installed projects file is not readable:" << file.error;
        m_backupPath = JsonFile::backupAside(m_filePath, u"corrupt"_s, JsonFile::BackupMode::Move);
        return LoadResult::RecoveredFromCorruption;
    }

    const int version = file.object.value(kVersionKey).toInt(0);
    if (version > kSchemaVersion) {
        qCWarning(lcInstall) << "Installed projects file has schema version" << version
                             << "which is newer than" << kSchemaVersion;
        m_backupPath = JsonFile::backupAside(m_filePath, u"newer"_s, JsonFile::BackupMode::Move);
        return LoadResult::RecoveredFromNewerSchema;
    }

    m_foreign = file.object;
    m_foreign.remove(kVersionKey);
    m_foreign.remove(kProjectsKey);

    const QJsonObject projects = file.object.value(kProjectsKey).toObject();
    for (auto it = projects.constBegin(); it != projects.constEnd(); ++it) {
        if (const auto project = fromJson(it.key(), it.value())) {
            m_projects.insert(project->projectId, *project);
        } else {
            qCWarning(lcInstall).noquote()
                << "Ignoring invalid record in the installed projects file:" << it.key();
        }
    }
    return LoadResult::Loaded;
}

bool InstalledProjects::contains(const QString& projectId) const
{
    QMutexLocker lock(&m_mutex);
    return m_projects.contains(projectId);
}

std::optional<InstalledProject> InstalledProjects::get(const QString& projectId) const
{
    QMutexLocker lock(&m_mutex);
    const auto it = m_projects.constFind(projectId);
    if (it == m_projects.cend())
        return std::nullopt;
    return *it;
}

QList<InstalledProject> InstalledProjects::all() const
{
    QMutexLocker lock(&m_mutex);
    QList<InstalledProject> list = m_projects.values();
    std::sort(list.begin(), list.end(),
        [](const InstalledProject& a, const InstalledProject& b) { return a.projectId < b.projectId; });
    return list;
}

bool InstalledProjects::upsert(const InstalledProject& project, QString* error)
{
    {
        QMutexLocker lock(&m_mutex);
        const auto previous = m_projects.constFind(project.projectId);
        const std::optional<InstalledProject> before =
            previous == m_projects.cend() ? std::nullopt : std::optional<InstalledProject>(*previous);

        m_projects.insert(project.projectId, project);
        if (!saveLocked(error)) {
            if (before)
                m_projects.insert(project.projectId, *before);
            else
                m_projects.remove(project.projectId);
            return false;
        }
    }
    emit changed(project.projectId);
    return true;
}

bool InstalledProjects::remove(const QString& projectId, QString* error)
{
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_projects.constFind(projectId);
        if (it == m_projects.cend())
            return true;

        const InstalledProject before = *it;
        m_projects.remove(projectId);
        if (!saveLocked(error)) {
            m_projects.insert(projectId, before);
            return false;
        }
    }
    emit changed(projectId);
    return true;
}

void InstalledProjects::recordStart(const QString& projectId, const QDateTime& when)
{
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_projects.find(projectId);
        if (it == m_projects.end())
            return;
        it->lastStartedAt = when.toUTC();
        QString error;
        if (!saveLocked(&error))
            qCWarning(lcInstall).noquote() << "Could not save the start time of" << projectId << ":" << error;
    }
    emit changed(projectId);
}

bool InstalledProjects::saveLocked(QString* error)
{
    QJsonObject root = m_foreign;
    root.insert(kVersionKey, kSchemaVersion);

    QJsonObject projects;
    for (auto it = m_projects.constBegin(); it != m_projects.constEnd(); ++it)
        projects.insert(it.key(), toJson(it.value()));
    root.insert(kProjectsKey, projects);

    QString detail;
    if (JsonFile::saveAtomic(m_filePath, root, &detail))
        return true;
    qCWarning(lcInstall).noquote() << "Could not save the installed projects file:" << detail;
    if (error != nullptr)
        *error = detail;
    return false;
}

} // namespace launcher
