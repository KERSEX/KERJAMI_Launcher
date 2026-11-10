#pragma once

#include "models/Release.h"

#include <QByteArray>
#include <QList>
#include <QString>

namespace launcher {

struct ReleaseListResult {
    bool ok = false;
    QString error; ///< Technical detail if the answer is not a list of releases.

    /// Stable releases in the order of the answer (GitHub lists the newest first).
    QList<Release> releases;

    /// Entries left out: drafts, pre-releases and tags that are not a stable version.
    int skipped = 0;
};

/// Reads GitHub's answer to "list releases". The answer is untrusted input:
/// only what the launcher needs is taken over, and every value is checked.
namespace ReleaseParser {

inline constexpr qsizetype kMaxReleases = 100;
inline constexpr qsizetype kMaxAssetsPerRelease = 100;
inline constexpr qsizetype kMaxNotesLength = 20000;

/// The largest download the launcher accepts.
inline constexpr qint64 kMaxAssetBytes = 2LL * 1024 * 1024 * 1024;

/// Stable means: not a draft, not marked as pre-release by GitHub, and a tag
/// that is a version without pre-release part. An asset is taken over if it
/// is uploaded, has a safe file name and a plausible size.
ReleaseListResult parseList(const QByteArray& json);

/// The release with the highest version. The order of the list does not
/// matter: a later hotfix for an old version must not count as the newest.
/// Returns nullptr for an empty list.
const Release* latest(const QList<Release>& releases);

/// The assets whose name fits the pattern; "*" stands for any characters,
/// upper and lower case are not told apart.
QList<ReleaseAsset> matchingAssets(const Release& release, const QString& pattern);

/// The largest checksum file the launcher reads.
inline constexpr qint64 kMaxChecksumFileBytes = 64 * 1024;

/// The checksum file of the release that belongs to @p asset, or nullptr.
///
/// Looked for by exact name (case ignored): first a file of its own,
/// "<asset>.sha256" or "<asset>.sha256sum", then a combined list "SHA256SUMS"
/// or "SHA256SUMS.txt". If a group holds more than one candidate nothing is
/// chosen: a launcher that guesses which checksum is meant verifies nothing.
/// @p bareHashAllowed tells whether the file is of the first kind.
const ReleaseAsset* checksumAsset(const Release& release, const ReleaseAsset& asset, bool* bareHashAllowed);

/// A plain file name that is safe to create on Windows: no path, no reserved
/// device name, at most 128 characters.
bool isValidAssetFileName(const QString& name);

} // namespace ReleaseParser

} // namespace launcher
