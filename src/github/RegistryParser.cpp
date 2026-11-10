#include "github/RegistryParser.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

constexpr qsizetype kMaxIdLength = 48;
constexpr qsizetype kMaxNameLength = 80;
constexpr qsizetype kMaxDescriptionLength = 400;
constexpr qsizetype kMaxCategoryLength = 32;
constexpr qsizetype kMaxTags = 8;
constexpr qsizetype kMaxTagLength = 24;
constexpr qsizetype kMaxExecutableLength = 200;

/// Control and invisible formatting characters have no place in names that are
/// shown to the user; direction overrides can disguise text.
bool hasUnsafeCharacter(const QString& text)
{
    return std::any_of(text.cbegin(), text.cend(), [](QChar c) {
        const QChar::Category category = c.category();
        return category == QChar::Other_Control || category == QChar::Other_Format;
    });
}

bool matchesWhole(const QString& pattern, const QString& text)
{
    // \A and \z, not ^ and $: "$" would accept a trailing newline.
    const QRegularExpression regex(u"\\A(?:"_s + pattern + u")\\z"_s);
    return regex.match(text).hasMatch();
}

/// "con", "CON.txt" and "Com1.exe" all name a device on Windows.
bool isDeviceName(const QString& name)
{
    const QString base = name.section(u'.', 0, 0).toUpper();
    if (base == "CON"_L1 || base == "PRN"_L1 || base == "AUX"_L1 || base == "NUL"_L1)
        return true;
    return base.size() == 4 && (base.startsWith("COM"_L1) || base.startsWith("LPT"_L1))
        && base.at(3).isDigit();
}

/// Reads the fields of one registry entry and remembers the first problem.
class EntryReader {
public:
    explicit EntryReader(const QJsonObject& object)
        : m_object(object)
    {
    }

    bool ok() const { return m_error.isEmpty(); }
    const QString& error() const { return m_error; }

    void fail(const QString& message)
    {
        if (m_error.isEmpty())
            m_error = message;
    }

    /// Display text: trimmed, within a length range, without control characters.
    QString text(QLatin1StringView key, qsizetype minLength, qsizetype maxLength)
    {
        const QString raw = string(key);
        if (!ok())
            return {};
        const QString trimmed = raw.trimmed();
        if (trimmed.size() < minLength || trimmed.size() > maxLength) {
            fail(u"\"%1\" must have %2 to %3 characters"_s.arg(key).arg(minLength).arg(maxLength));
            return {};
        }
        if (hasUnsafeCharacter(trimmed)) {
            fail(u"\"%1\" contains control characters"_s.arg(key));
            return {};
        }
        return trimmed;
    }

    /// Identifier or file name: taken as it is, never trimmed or corrected.
    QString identifier(QLatin1StringView key, bool (*isValid)(const QString&), const QString& rule)
    {
        const QString raw = string(key);
        if (!ok())
            return {};
        if (!isValid(raw)) {
            fail(u"\"%1\" is not valid (%2)"_s.arg(key, rule));
            return {};
        }
        return raw;
    }

    bool flag(QLatin1StringView key, bool required, bool fallback)
    {
        if (!ok())
            return fallback;
        const QJsonValue value = m_object.value(key);
        if (value.isUndefined()) {
            if (required)
                fail(u"\"%1\" is missing"_s.arg(key));
            return fallback;
        }
        if (!value.isBool()) {
            fail(u"\"%1\" must be true or false"_s.arg(key));
            return fallback;
        }
        return value.toBool();
    }

    QStringList tags(QLatin1StringView key)
    {
        if (!ok())
            return {};
        const QJsonValue value = m_object.value(key);
        if (value.isUndefined())
            return {};
        if (!value.isArray()) {
            fail(u"\"%1\" must be a list"_s.arg(key));
            return {};
        }

        const QJsonArray array = value.toArray();
        if (array.size() > kMaxTags) {
            fail(u"\"%1\" has more than %2 entries"_s.arg(key).arg(kMaxTags));
            return {};
        }

        QStringList result;
        for (const QJsonValue& item : array) {
            if (!item.isString()) {
                fail(u"\"%1\" must contain only strings"_s.arg(key));
                return {};
            }
            const QString tag = item.toString().trimmed();
            if (tag.isEmpty() || tag.size() > kMaxTagLength || hasUnsafeCharacter(tag)) {
                fail(
                    u"\"%1\" has an entry that is empty, longer than %2 characters or has control characters"_s
                        .arg(key)
                        .arg(kMaxTagLength));
                return {};
            }
            if (!result.contains(tag, Qt::CaseInsensitive))
                result.append(tag);
        }
        return result;
    }

    /// Optional text that must equal @p expected when present.
    void expectIfPresent(QLatin1StringView key, QLatin1StringView expected)
    {
        if (!ok())
            return;
        const QJsonValue value = m_object.value(key);
        if (value.isUndefined())
            return;
        if (!value.isString() || value.toString() != expected)
            fail(u"\"%1\" must be \"%2\""_s.arg(key, expected));
    }

private:
    QString string(QLatin1StringView key)
    {
        if (!ok())
            return {};
        const QJsonValue value = m_object.value(key);
        if (value.isUndefined()) {
            fail(u"\"%1\" is missing"_s.arg(key));
            return {};
        }
        if (!value.isString()) {
            fail(u"\"%1\" must be a string"_s.arg(key));
            return {};
        }
        return value.toString();
    }

    const QJsonObject& m_object;
    QString m_error;
};

/// Short, log-safe form of an id for messages about entries that failed.
QString idForLog(const QJsonValue& entry)
{
    if (!entry.isObject())
        return {};
    const QJsonValue id = entry.toObject().value("id"_L1);
    if (!id.isString())
        return {};
    QString text = id.toString().left(kMaxIdLength);
    for (QChar& c : text) {
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format)
            c = u'?';
    }
    return text;
}

std::optional<Project> parseEntry(const QJsonValue& value, QString* error)
{
    if (!value.isObject()) {
        *error = u"entry is not an object"_s;
        return std::nullopt;
    }

    const QJsonObject object = value.toObject();
    EntryReader reader(object);

    Project project;
    project.id = reader.identifier("id"_L1, RegistryParser::isValidProjectId,
        u"lowercase letters, digits and single hyphens, at most 48 characters"_s);
    project.name = reader.text("name"_L1, 1, kMaxNameLength);
    project.description = reader.text("description"_L1, 1, kMaxDescriptionLength);
    project.owner = reader.identifier("owner"_L1, RegistryParser::isValidOwner, u"a GitHub account name"_s);
    project.repository =
        reader.identifier("repository"_L1, RegistryParser::isValidRepository, u"a GitHub repository name"_s);
    project.category = reader.text("category"_L1, 1, kMaxCategoryLength);
    project.tags = reader.tags("tags"_L1);
    project.enabled = reader.flag("enabled"_L1, true, false);
    project.featured = reader.flag("featured"_L1, false, false);
    reader.expectIfPresent("release_channel"_L1, "stable"_L1);
    project.releaseChannel = ReleaseChannel::Stable;
    project.assetPattern = reader.identifier("asset_pattern"_L1, RegistryParser::isValidAssetPattern,
        u"a ZIP file name without a path; * is the only wildcard"_s);
    project.executable = reader.identifier("executable"_L1, RegistryParser::isValidExecutablePath,
        u"a relative path with / separators ending in .exe"_s);
    project.installDirectory = reader.identifier("install_directory"_L1,
        RegistryParser::isValidInstallDirectory, u"a single, Windows-safe folder name"_s);

    if (!reader.ok()) {
        *error = reader.error();
        return std::nullopt;
    }
    return project;
}

RegistryParseResult failure(RegistryParseResult::Status status, const QString& error)
{
    RegistryParseResult result;
    result.status = status;
    result.error = error;
    return result;
}

} // namespace

namespace RegistryParser {

bool isReservedWindowsName(const QString& name)
{
    return isDeviceName(name);
}

bool isValidProjectId(const QString& id)
{
    return id.size() <= kMaxIdLength && matchesWhole(u"[a-z0-9]+(?:-[a-z0-9]+)*"_s, id);
}

bool isValidOwner(const QString& owner)
{
    return matchesWhole(u"[A-Za-z0-9](?:[A-Za-z0-9-]{0,37}[A-Za-z0-9])?"_s, owner);
}

bool isValidRepository(const QString& repository)
{
    if (repository == "."_L1 || repository == ".."_L1 || repository.endsWith(".git"_L1, Qt::CaseInsensitive))
        return false;
    return matchesWhole(u"[A-Za-z0-9._-]{1,100}"_s, repository);
}

bool isValidInstallDirectory(const QString& directory)
{
    if (directory.endsWith(u'.') || isReservedWindowsName(directory))
        return false;
    return matchesWhole(u"[A-Za-z0-9][A-Za-z0-9._-]{0,63}"_s, directory);
}

bool isValidAssetPattern(const QString& pattern)
{
    return pattern.endsWith(".zip"_L1, Qt::CaseInsensitive)
        && matchesWhole(u"[A-Za-z0-9._*+()-]{1,128}"_s, pattern);
}

bool isValidExecutablePath(const QString& path)
{
    if (path.isEmpty() || path.size() > kMaxExecutableLength
        || !path.endsWith(".exe"_L1, Qt::CaseInsensitive))
        return false;
    // No leading "/", no drive letter or stream marker (":"), no backslashes.
    if (path.startsWith(u'/') || path.contains(u':') || path.contains(u'\\'))
        return false;

    const QStringList segments = path.split(u'/');
    return std::none_of(segments.cbegin(), segments.cend(), [](const QString& segment) {
        if (segment.isEmpty() || segment == "."_L1 || segment == ".."_L1)
            return true;
        if (segment.endsWith(u'.') || segment.endsWith(u' ') || isReservedWindowsName(segment))
            return true;
        return std::any_of(segment.cbegin(), segment.cend(), [](QChar c) {
            return c.unicode() < 0x20 || c.unicode() == 0x7F || QStringView(u"*?\"<>|").contains(c);
        });
    });
}

RegistryParseResult parse(const QByteArray& json)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return failure(RegistryParseResult::Status::InvalidJson, parseError.errorString());
    if (!document.isObject())
        return failure(RegistryParseResult::Status::InvalidStructure, u"top-level value is not an object"_s);
    return parse(document.object());
}

RegistryParseResult parse(const QJsonObject& root)
{
    const QJsonValue versionValue = root.value("schema_version"_L1);
    const double versionNumber = versionValue.toDouble(-1);
    if (!versionValue.isDouble() || versionNumber != static_cast<int>(versionNumber) || versionNumber < 1) {
        RegistryParseResult result = failure(RegistryParseResult::Status::InvalidStructure,
            u"\"schema_version\" is missing or not a positive integer"_s);
        result.document = root;
        return result;
    }

    const int schemaVersion = static_cast<int>(versionNumber);
    if (schemaVersion > kSupportedSchemaVersion) {
        RegistryParseResult result = failure(RegistryParseResult::Status::UnsupportedVersion,
            u"schema_version %1 is newer than the supported version %2"_s.arg(schemaVersion)
                .arg(kSupportedSchemaVersion));
        result.schemaVersion = schemaVersion;
        result.document = root;
        return result;
    }

    const QJsonValue projectsValue = root.value("projects"_L1);
    if (!projectsValue.isArray()) {
        RegistryParseResult result = failure(
            RegistryParseResult::Status::InvalidStructure, u"\"projects\" is missing or not a list"_s);
        result.document = root;
        return result;
    }
    const QJsonArray entries = projectsValue.toArray();
    if (entries.size() > kMaxProjects) {
        RegistryParseResult result = failure(RegistryParseResult::Status::InvalidStructure,
            u"\"projects\" has more than %1 entries"_s.arg(kMaxProjects));
        result.document = root;
        return result;
    }

    RegistryParseResult result;
    result.status = RegistryParseResult::Status::Ok;
    result.schemaVersion = schemaVersion;
    result.document = root;
    if (root.value("registry_name"_L1).isString())
        result.registryName = root.value("registry_name"_L1).toString().trimmed().left(kMaxNameLength);

    struct Candidate {
        int index;
        Project project;
    };
    QList<Candidate> candidates;
    for (int index = 0; index < entries.size(); ++index) {
        QString error;
        std::optional<Project> project = parseEntry(entries.at(index), &error);
        if (project)
            candidates.append({index, *project});
        else
            result.issues.append({index, idForLog(entries.at(index)), error});
    }

    // Count over all valid entries, enabled or not: a disabled entry that
    // shares an id with an enabled one is just as ambiguous.
    QHash<QString, int> idCount;
    QHash<QString, int> directoryCount;
    for (const Candidate& candidate : std::as_const(candidates)) {
        ++idCount[candidate.project.id];
        ++directoryCount[candidate.project.installDirectory.toLower()];
    }

    for (const Candidate& candidate : std::as_const(candidates)) {
        const Project& project = candidate.project;
        if (idCount.value(project.id) > 1) {
            result.issues.append({candidate.index, project.id, u"\"id\" is used by more than one entry"_s});
        } else if (directoryCount.value(project.installDirectory.toLower()) > 1) {
            result.issues.append(
                {candidate.index, project.id, u"\"install_directory\" is used by more than one entry"_s});
        } else if (project.enabled) {
            result.projects.append(project);
        } else {
            ++result.disabledCount;
        }
    }

    std::sort(result.issues.begin(), result.issues.end(),
        [](const RegistryIssue& a, const RegistryIssue& b) { return a.index < b.index; });
    return result;
}

} // namespace RegistryParser

} // namespace launcher
