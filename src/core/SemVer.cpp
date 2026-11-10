#include "core/SemVer.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher {

namespace {

constexpr qsizetype kMaxLength = 128;
constexpr qsizetype kMaxDigits = 9; // keeps every number inside an int

std::strong_ordering fromInt(int value)
{
    if (value < 0)
        return std::strong_ordering::less;
    return value > 0 ? std::strong_ordering::greater : std::strong_ordering::equal;
}

bool isNumeric(const QString& identifier)
{
    return std::all_of(
        identifier.cbegin(), identifier.cend(), [](QChar c) { return c >= u'0' && c <= u'9'; });
}

/// Pre-release identifiers: numbers compare as numbers and rank below text,
/// text compares character by character.
std::strong_ordering compareIdentifier(const QString& a, const QString& b)
{
    const bool aNumeric = isNumeric(a);
    const bool bNumeric = isNumeric(b);
    if (aNumeric && bNumeric) {
        // No leading zeros, so the longer number is the larger one. No overflow possible.
        if (a.size() != b.size())
            return a.size() <=> b.size();
        return fromInt(a.compare(b));
    }
    if (aNumeric)
        return std::strong_ordering::less;
    if (bNumeric)
        return std::strong_ordering::greater;
    return fromInt(a.compare(b));
}

std::optional<int> toNumber(const QString& digits)
{
    if (digits.size() > kMaxDigits)
        return std::nullopt;
    return digits.toInt();
}

} // namespace

std::optional<SemVer> SemVer::parse(const QString& text)
{
    QString version = text;
    if (version.size() > kMaxLength)
        return std::nullopt;
    if (version.startsWith(u'v') || version.startsWith(u'V'))
        version.remove(0, 1);

    // The expression from semver.org, anchored with \A and \z.
    static const QRegularExpression pattern(u"\\A(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)"
                                            u"(?:-((?:0|[1-9][0-9]*|[0-9]*[a-zA-Z-][0-9a-zA-Z-]*)(?:\\.(?:0|["
                                            u"1-9][0-9]*|[0-9]*[a-zA-Z-][0-9a-zA-Z-]*))*))?"
                                            u"(?:\\+([0-9a-zA-Z-]+(?:\\.[0-9a-zA-Z-]+)*))?\\z"_s);

    const QRegularExpressionMatch match = pattern.match(version);
    if (!match.hasMatch())
        return std::nullopt;

    const std::optional<int> major = toNumber(match.captured(1));
    const std::optional<int> minor = toNumber(match.captured(2));
    const std::optional<int> patch = toNumber(match.captured(3));
    if (!major || !minor || !patch)
        return std::nullopt;

    SemVer result;
    result.major = *major;
    result.minor = *minor;
    result.patch = *patch;
    if (!match.captured(4).isEmpty())
        result.preRelease = match.captured(4).split(u'.');
    result.build = match.captured(5);
    return result;
}

QString SemVer::toString() const
{
    QString text = u"%1.%2.%3"_s.arg(major).arg(minor).arg(patch);
    if (!preRelease.isEmpty())
        text += u'-' + preRelease.join(u'.');
    if (!build.isEmpty())
        text += u'+' + build;
    return text;
}

std::strong_ordering operator<=>(const SemVer& a, const SemVer& b)
{
    if (a.major != b.major)
        return a.major <=> b.major;
    if (a.minor != b.minor)
        return a.minor <=> b.minor;
    if (a.patch != b.patch)
        return a.patch <=> b.patch;

    // 1.0.0-rc.1 comes before 1.0.0.
    if (a.isPreRelease() != b.isPreRelease())
        return a.isPreRelease() ? std::strong_ordering::less : std::strong_ordering::greater;

    const qsizetype shared = std::min(a.preRelease.size(), b.preRelease.size());
    for (qsizetype i = 0; i < shared; ++i) {
        const std::strong_ordering order = compareIdentifier(a.preRelease.at(i), b.preRelease.at(i));
        if (order != std::strong_ordering::equal)
            return order;
    }
    // With equal identifiers up to here, the shorter list is the smaller one.
    return a.preRelease.size() <=> b.preRelease.size();
}

bool operator==(const SemVer& a, const SemVer& b)
{
    return (a <=> b) == std::strong_ordering::equal;
}

} // namespace launcher
