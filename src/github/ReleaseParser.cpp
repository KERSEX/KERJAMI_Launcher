#include "github/ReleaseParser.h"

#include "github/RegistryParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

constexpr qsizetype kMaxTitleLength = 200;

/// "sha256:" followed by 64 hex digits. Any other algorithm or shape counts as no digest.
QString parseDigest(const QJsonValue& value)
{
    if (!value.isString())
        return {};
    static const QRegularExpression pattern(u"\\Asha256:([0-9a-fA-F]{64})\\z"_s);
    const QRegularExpressionMatch match = pattern.match(value.toString());
    return match.hasMatch() ? match.captured(1).toLower() : QString();
}

std::optional<ReleaseAsset> parseAsset(const QJsonValue& value)
{
    if (!value.isObject())
        return std::nullopt;
    const QJsonObject object = value.toObject();

    // Only finished uploads can be downloaded.
    if (object.value("state"_L1).toString() != "uploaded"_L1)
        return std::nullopt;

    const QString name = object.value("name"_L1).toString();
    if (!ReleaseParser::isValidAssetFileName(name))
        return std::nullopt;

    const QJsonValue size = object.value("size"_L1);
    if (!size.isDouble())
        return std::nullopt;
    const qint64 bytes = static_cast<qint64>(size.toDouble());
    if (bytes < 1 || bytes > ReleaseParser::kMaxAssetBytes)
        return std::nullopt;

    ReleaseAsset asset;
    asset.name = name;
    asset.size = bytes;
    asset.sha256 = parseDigest(object.value("digest"_L1));
    return asset;
}

std::optional<Release> parseRelease(const QJsonObject& object)
{
    if (object.value("draft"_L1).toBool(false) || object.value("prerelease"_L1).toBool(false))
        return std::nullopt;

    const QString tag = object.value("tag_name"_L1).toString();
    const std::optional<SemVer> version = SemVer::parse(tag);
    if (!version || version->isPreRelease())
        return std::nullopt;

    Release release;
    release.tag = tag;
    release.version = *version;
    release.title = object.value("name"_L1).toString().trimmed().left(kMaxTitleLength);
    release.notes = object.value("body"_L1).toString().left(ReleaseParser::kMaxNotesLength);
    release.publishedAt = QDateTime::fromString(object.value("published_at"_L1).toString(), Qt::ISODate);

    const QJsonArray assets = object.value("assets"_L1).toArray();
    for (qsizetype i = 0; i < assets.size() && i < ReleaseParser::kMaxAssetsPerRelease; ++i) {
        if (std::optional<ReleaseAsset> asset = parseAsset(assets.at(i)))
            release.assets.append(*asset);
    }
    return release;
}

} // namespace

namespace ReleaseParser {

bool isValidAssetFileName(const QString& name)
{
    static const QRegularExpression pattern(u"\\A[A-Za-z0-9][A-Za-z0-9._+()-]{0,127}\\z"_s);
    return pattern.match(name).hasMatch() && !name.endsWith(u'.')
        && !RegistryParser::isReservedWindowsName(name);
}

ReleaseListResult parseList(const QByteArray& json)
{
    ReleaseListResult result;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.error = parseError.errorString();
        return result;
    }
    if (!document.isArray()) {
        result.error = u"the answer is not a list of releases"_s;
        return result;
    }

    const QJsonArray entries = document.array();
    result.ok = true;
    for (qsizetype i = 0; i < entries.size(); ++i) {
        if (i >= kMaxReleases)
            break;
        std::optional<Release> release;
        if (entries.at(i).isObject())
            release = parseRelease(entries.at(i).toObject());
        if (release)
            result.releases.append(*release);
        else
            ++result.skipped;
    }
    return result;
}

const Release* latest(const QList<Release>& releases)
{
    const Release* best = nullptr;
    for (const Release& release : releases) {
        // Strictly greater: of two releases with the same version the first one stays.
        if (!best || release.version > best->version)
            best = &release;
    }
    return best;
}

QList<ReleaseAsset> matchingAssets(const Release& release, const QString& pattern)
{
    // Only "*" is a wildcard. Everything else, "?" and "[" included, is taken literally.
    QStringList parts = pattern.split(u'*');
    for (QString& part : parts)
        part = QRegularExpression::escape(part);
    const QRegularExpression regex(
        u"\\A"_s + parts.join(u".*"_s) + u"\\z"_s, QRegularExpression::CaseInsensitiveOption);

    QList<ReleaseAsset> matches;
    for (const ReleaseAsset& asset : release.assets) {
        if (regex.match(asset.name).hasMatch())
            matches.append(asset);
    }
    return matches;
}

} // namespace ReleaseParser

} // namespace launcher
