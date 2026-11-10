#pragma once

#include <QString>
#include <QStringList>

#include <compare>
#include <optional>

namespace launcher {

/// A version number according to Semantic Versioning 2.0.0 (semver.org).
///
/// Used to decide whether a release is newer than what is installed. The order
/// follows the specification: numbers first, a pre-release ranks below the
/// release it leads to, and build metadata has no influence.
class SemVer {
public:
    int major = 0;
    int minor = 0;
    int patch = 0;
    QStringList preRelease; ///< Identifiers after "-", for example {"rc", "1"}.
    QString build;          ///< Metadata after "+". Ignored when comparing.

    /// Reads "1.2.3", "1.2.3-rc.1" or "1.2.3+build5", with an optional leading
    /// "v" or "V" as in git tags. Returns nothing for anything that is not a
    /// valid version, including leading zeros ("01.2.3") and numbers that do
    /// not fit in nine digits.
    static std::optional<SemVer> parse(const QString& text);

    bool isPreRelease() const { return !preRelease.isEmpty(); }

    /// "1.2.3-rc.1+build5", without a leading "v".
    QString toString() const;
};

std::strong_ordering operator<=>(const SemVer& a, const SemVer& b);

/// True if the versions have the same precedence. Versions that differ only in
/// build metadata are equal.
bool operator==(const SemVer& a, const SemVer& b);

} // namespace launcher
