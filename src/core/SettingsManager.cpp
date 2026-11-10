#include "core/SettingsManager.h"

#include "core/JsonFile.h"
#include "core/Logger.h"

#include <QDir>
#include <QJsonValue>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

constexpr auto kVersionKey = "schema_version"_L1;

constexpr auto kAppearance = "appearance"_L1;
constexpr auto kTheme = "theme"_L1;

constexpr auto kStartup = "startup"_L1;
constexpr auto kStartPage = "start_page"_L1;

constexpr auto kRegistry = "registry"_L1;
constexpr auto kRegistryAutoCheck = "auto_check"_L1;

constexpr auto kUpdates = "updates"_L1;
constexpr auto kCheckInterval = "check_interval_minutes"_L1;
constexpr auto kLauncherAutoCheck = "launcher_auto_check"_L1;

QString themeToString(ThemeId theme)
{
    switch (theme) {
    case ThemeId::Dark:
        return u"dark"_s;
    }
    return u"dark"_s;
}

ThemeId themeFromString(const QString&)
{
    // Only one theme exists. Unknown names fall back to it.
    return ThemeId::Dark;
}

QString startPageToString(StartPage page)
{
    switch (page) {
    case StartPage::Dashboard:
        return u"dashboard"_s;
    case StartPage::Library:
        return u"library"_s;
    }
    return u"dashboard"_s;
}

StartPage startPageFromString(const QString& text)
{
    return text == "library"_L1 ? StartPage::Library : StartPage::Dashboard;
}

bool readBool(const QJsonObject& section, QLatin1StringView key, bool fallback)
{
    const QJsonValue value = section.value(key);
    return value.isBool() ? value.toBool() : fallback;
}

int clampInterval(int minutes)
{
    return std::clamp(
        minutes, LauncherSettings::kMinCheckIntervalMinutes, LauncherSettings::kMaxCheckIntervalMinutes);
}

/// Values of the wrong type or out of range fall back to the defaults instead
/// of failing the whole file: one bad entry must not discard the rest.
LauncherSettings settingsFromJson(const QJsonObject& root)
{
    LauncherSettings settings;

    const QJsonObject appearance = root.value(kAppearance).toObject();
    settings.theme = themeFromString(appearance.value(kTheme).toString());

    const QJsonObject startup = root.value(kStartup).toObject();
    settings.startPage = startPageFromString(startup.value(kStartPage).toString());

    const QJsonObject registry = root.value(kRegistry).toObject();
    settings.registryAutoCheck = readBool(registry, kRegistryAutoCheck, settings.registryAutoCheck);

    const QJsonObject updates = root.value(kUpdates).toObject();
    settings.launcherUpdateAutoCheck =
        readBool(updates, kLauncherAutoCheck, settings.launcherUpdateAutoCheck);
    const QJsonValue interval = updates.value(kCheckInterval);
    if (interval.isDouble())
        settings.updateCheckIntervalMinutes =
            clampInterval(interval.toInt(settings.updateCheckIntervalMinutes));

    return settings;
}

void setInSection(
    QJsonObject& root, QLatin1StringView section, QLatin1StringView key, const QJsonValue& value)
{
    QJsonObject object = root.value(section).toObject();
    object.insert(key, value);
    root.insert(section, object);
}

/// Writes the known settings into @p root and keeps everything else in it.
void writeSettings(QJsonObject& root, const LauncherSettings& settings)
{
    root.insert(kVersionKey, SettingsManager::kSchemaVersion);
    setInSection(root, kAppearance, kTheme, themeToString(settings.theme));
    setInSection(root, kStartup, kStartPage, startPageToString(settings.startPage));
    setInSection(root, kRegistry, kRegistryAutoCheck, settings.registryAutoCheck);
    setInSection(root, kUpdates, kCheckInterval, settings.updateCheckIntervalMinutes);
    setInSection(root, kUpdates, kLauncherAutoCheck, settings.launcherUpdateAutoCheck);
}

/// Returns the schema version of a file: 0 if it carries no version stamp,
/// -1 if the stamp is not a non-negative integer.
int schemaVersionOf(const QJsonObject& root)
{
    const QJsonValue value = root.value(kVersionKey);
    if (value.isUndefined())
        return 0;
    const int version = value.toInt(-1);
    return version >= 0 ? version : -1;
}

/// Upgrades the file layout one schema version at a time.
QJsonObject migrate(QJsonObject root, int fromVersion)
{
    // Version 0 is a file without a version stamp (for example written by
    // hand). Its layout equals version 1, so only the stamp is added.
    //
    // Later versions add their step here:
    //     if (fromVersion < 2) { ...restructure root... }
    Q_UNUSED(fromVersion)
    root.insert(kVersionKey, SettingsManager::kSchemaVersion);
    return root;
}

} // namespace

SettingsManager::SettingsManager(const QString& filePath, QObject* parent)
    : QObject(parent)
    , m_filePath(filePath)
{
}

SettingsManager::LoadResult SettingsManager::load()
{
    m_backupPath.clear();

    LoadResult result = LoadResult::Loaded;
    QJsonObject raw;

    const JsonFile::LoadResult file = JsonFile::load(m_filePath);
    switch (file.status) {
    case JsonFile::Status::Ok:
        raw = file.object;
        break;
    case JsonFile::Status::NotFound:
        result = LoadResult::CreatedDefaults;
        break;
    case JsonFile::Status::Corrupt:
    case JsonFile::Status::ReadError:
        qCWarning(lcSettings).noquote() << "Settings file is unreadable:" << file.error;
        result = LoadResult::RecoveredFromCorruption;
        break;
    }

    if (result == LoadResult::Loaded) {
        const int version = schemaVersionOf(raw);
        if (version < 0) {
            qCWarning(lcSettings) << "Settings file has an invalid schema version";
            result = LoadResult::RecoveredFromCorruption;
        } else if (version > kSchemaVersion) {
            // The layout of a newer launcher is unknown. Guessing at it could
            // silently change what the user configured, so start clean and
            // keep the file for the newer version.
            qCWarning(lcSettings) << "Settings file was written by a newer version, schema" << version;
            m_backupPath =
                JsonFile::backupAside(m_filePath, u"v%1"_s.arg(version), JsonFile::BackupMode::Move);
            raw = {};
            result = LoadResult::RecoveredFromNewerSchema;
        } else if (version < kSchemaVersion) {
            m_backupPath =
                JsonFile::backupAside(m_filePath, u"v%1"_s.arg(version), JsonFile::BackupMode::Copy);
            raw = migrate(raw, version);
            qCInfo(lcSettings) << "Settings migrated from schema" << version << "to" << kSchemaVersion;
            result = LoadResult::Migrated;
        }
    }

    if (result == LoadResult::RecoveredFromCorruption) {
        m_backupPath = JsonFile::backupAside(m_filePath, u"corrupt"_s, JsonFile::BackupMode::Move);
        raw = {};
    }

    m_raw = raw;
    m_settings = settingsFromJson(raw);

    if (result == LoadResult::Loaded) {
        qCInfo(lcSettings) << "Settings loaded";
    } else {
        if (result == LoadResult::CreatedDefaults)
            qCInfo(lcSettings) << "No settings file yet; writing defaults";
        if (!m_backupPath.isEmpty())
            qCInfo(lcSettings).noquote()
                << "Previous settings file kept as" << QDir::toNativeSeparators(m_backupPath);
        save();
    }
    return result;
}

bool SettingsManager::setTheme(ThemeId theme)
{
    return assign(&LauncherSettings::theme, theme);
}

bool SettingsManager::setStartPage(StartPage page)
{
    return assign(&LauncherSettings::startPage, page);
}

bool SettingsManager::setRegistryAutoCheck(bool enabled)
{
    return assign(&LauncherSettings::registryAutoCheck, enabled);
}

bool SettingsManager::setLauncherUpdateAutoCheck(bool enabled)
{
    return assign(&LauncherSettings::launcherUpdateAutoCheck, enabled);
}

bool SettingsManager::setUpdateCheckIntervalMinutes(int minutes)
{
    return assign(&LauncherSettings::updateCheckIntervalMinutes, clampInterval(minutes));
}

template <typename T> bool SettingsManager::assign(T LauncherSettings::* member, T value)
{
    if (m_settings.*member == value)
        return true;

    m_settings.*member = value;
    const bool saved = save();
    emit settingsChanged();
    return saved;
}

bool SettingsManager::save()
{
    QJsonObject root = m_raw;
    writeSettings(root, m_settings);

    QString error;
    if (!JsonFile::saveAtomic(m_filePath, root, &error)) {
        qCWarning(lcSettings).noquote() << "Settings could not be saved:" << error;
        emit saveFailed(error);
        return false;
    }

    m_raw = root;
    return true;
}

} // namespace launcher
