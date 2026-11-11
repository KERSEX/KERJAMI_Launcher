#include "updater/UpdateManifest.h"

#include "core/SemVer.h"
#include "security/ArchiveValidator.h"
#include "security/HashVerifier.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

using namespace Qt::StringLiterals;

namespace launcher {

const ManifestFile* UpdateManifest::find(const QString& path) const
{
    for (const ManifestFile& file : files) {
        if (file.path.compare(path, Qt::CaseInsensitive) == 0)
            return &file;
    }
    return nullptr;
}

namespace UpdateManifestFormat {

namespace {

const QStringList kProtectedTopFolders = {
    u"launcher"_s, u"library"_s, u"downloads"_s, u"temp"_s, u"cache"_s, u"logs"_s, u"updater"_s};

ManifestParseResult refuse(const QString& error)
{
    ManifestParseResult result;
    result.error = error;
    return result;
}

} // namespace

QString checkedPath(const QString& path, QString* reason)
{
    const auto fail = [reason](const QString& why) {
        if (reason != nullptr)
            *reason = why;
        return QString();
    };

    // The same rules as for names in an archive: no "..", no drive, no device
    // names, nothing Windows would change. Backslashes are not accepted at all
    // here: a manifest is written by our own packaging, so it is exact.
    if (path.contains(u'\\'))
        return fail(u"backslash in the path"_s);

    ArchiveValidator::Limits limits;
    bool isDirectory = false;
    QString why;
    const QString normalized = ArchiveValidator::normalizeEntryPath(path, limits, &isDirectory, &why);
    if (normalized.isEmpty())
        return fail(why);
    if (isDirectory || normalized != path)
        return fail(u"not a plain file path"_s);
    if (isProtectedPath(normalized))
        return fail(u"lies in a folder the update must not touch"_s);
    return normalized;
}

bool isProtectedPath(const QString& relativePath)
{
    const QString lower = relativePath.toLower();
    if (lower == kFileName)
        return true;
    for (const QString& folder : kProtectedTopFolders) {
        if (lower == folder || lower.startsWith(folder + u'/'))
            return true;
    }
    return false;
}

ManifestParseResult parse(const QByteArray& json, const ManifestExpectations& expectations)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return refuse(u"the manifest is not a JSON object"_s);
    const QJsonObject root = document.object();

    UpdateManifest manifest;

    const QJsonValue schema = root.value(u"schema_version"_s);
    if (!schema.isDouble() || schema.toInt(-1) < 1)
        return refuse(u"the manifest has no schema_version"_s);
    manifest.schemaVersion = schema.toInt();
    if (manifest.schemaVersion > kSchemaVersion)
        return refuse(
            u"the manifest has schema version %1; this launcher knows %2"_s.arg(manifest.schemaVersion)
                .arg(kSchemaVersion));
    if (manifest.schemaVersion != kSchemaVersion)
        return refuse(u"unsupported schema_version %1"_s.arg(manifest.schemaVersion));

    manifest.product = root.value(u"product"_s).toString();
    if (manifest.product != expectations.product)
        return refuse(
            u"the package is for \"%1\", not for \"%2\""_s.arg(manifest.product, expectations.product));

    manifest.version = root.value(u"version"_s).toString();
    const std::optional<SemVer> version = SemVer::parse(manifest.version);
    if (!version || manifest.version != version->toString())
        return refuse(u"\"%1\" is not a version"_s.arg(manifest.version));

    QString reason;
    manifest.launcher = checkedPath(root.value(u"launcher"_s).toString(), &reason);
    if (manifest.launcher.isEmpty())
        return refuse(u"launcher: %1"_s.arg(reason));
    if (manifest.launcher.compare(expectations.launcherFile, Qt::CaseInsensitive) != 0)
        return refuse(u"the launcher program is \"%1\", expected \"%2\""_s.arg(
            manifest.launcher, expectations.launcherFile));

    const QJsonValue files = root.value(u"files"_s);
    if (!files.isArray() || files.toArray().isEmpty() || files.toArray().size() > kMaxFiles)
        return refuse(u"\"files\" must hold 1 to %1 entries"_s.arg(kMaxFiles));

    QSet<QString> seen;
    for (const QJsonValue& value : files.toArray()) {
        if (!value.isObject())
            return refuse(u"an entry of \"files\" is not an object"_s);
        const QJsonObject entry = value.toObject();

        ManifestFile file;
        file.path = checkedPath(entry.value(u"path"_s).toString(), &reason);
        if (file.path.isEmpty())
            return refuse(u"file \"%1\": %2"_s.arg(entry.value(u"path"_s).toString(), reason));
        if (seen.contains(file.path.toLower()))
            return refuse(u"file \"%1\" is listed twice"_s.arg(file.path));
        seen.insert(file.path.toLower());

        const QJsonValue size = entry.value(u"size"_s);
        if (!size.isDouble() || size.toDouble() < 0 || size.toDouble() > double(kMaxFileSize)
            || size.toDouble() != double(qint64(size.toDouble())))
            return refuse(u"file \"%1\": invalid size"_s.arg(file.path));
        file.size = qint64(size.toDouble());

        const QString hash = entry.value(u"sha256"_s).toString();
        if (!HashVerifier::isValidSha256(hash))
            return refuse(u"file \"%1\": invalid sha256"_s.arg(file.path));
        file.sha256 = hash.toLower();

        manifest.files.append(file);
    }

    if (!manifest.find(manifest.launcher))
        return refuse(u"the launcher program \"%1\" is not in the file list"_s.arg(manifest.launcher));

    const QJsonValue remove = root.value(u"remove"_s);
    if (!remove.isUndefined()) {
        if (!remove.isArray() || remove.toArray().size() > kMaxRemove)
            return refuse(u"\"remove\" must be a list of at most %1 paths"_s.arg(kMaxRemove));
        for (const QJsonValue& value : remove.toArray()) {
            const QString path = checkedPath(value.toString(), &reason);
            if (path.isEmpty())
                return refuse(u"remove \"%1\": %2"_s.arg(value.toString(), reason));
            if (seen.contains(path.toLower()))
                return refuse(u"\"%1\" is both installed and removed"_s.arg(path));
            seen.insert(path.toLower());
            manifest.remove.append(path);
        }
    }

    ManifestParseResult result;
    result.ok = true;
    result.manifest = manifest;
    return result;
}

QByteArray toJson(const UpdateManifest& manifest)
{
    QJsonArray files;
    for (const ManifestFile& file : manifest.files) {
        files.append(
            QJsonObject {{u"path"_s, file.path}, {u"size"_s, file.size}, {u"sha256"_s, file.sha256}});
    }
    QJsonObject root {{u"schema_version"_s, manifest.schemaVersion}, {u"product"_s, manifest.product},
        {u"version"_s, manifest.version}, {u"launcher"_s, manifest.launcher}, {u"files"_s, files}};
    if (!manifest.remove.isEmpty())
        root.insert(u"remove"_s, QJsonArray::fromStringList(manifest.remove));
    return QJsonDocument(root).toJson();
}

} // namespace UpdateManifestFormat

} // namespace launcher
