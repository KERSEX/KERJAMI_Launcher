#pragma once

#include "core/SemVer.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

namespace launcher {

/// A file attached to a GitHub release.
struct ReleaseAsset {
    QString name;
    qint64 size = 0;
    /// SHA-256 as lowercase hex, taken from the digest GitHub computed for the
    /// asset. Empty if GitHub gave none, which is the case for assets uploaded
    /// before it started to do so. An asset without a hash cannot be verified.
    QString sha256;

    bool operator==(const ReleaseAsset&) const = default;
};

/// A stable release of a project.
struct Release {
    QString tag;
    SemVer version;
    QString title;
    QString notes; ///< The release notes as plain text (Markdown source), shortened if very long.
    QDateTime publishedAt;
    QList<ReleaseAsset> assets;
};

/// A checksum file of the release that belongs to the asset to install.
struct ChecksumFile {
    QString name;
    QUrl url; ///< Built the same way as the asset's address.

    /// True for a file that belongs to this one asset ("<asset>.sha256"): it may
    /// consist of the hash alone. A combined list ("SHA256SUMS") must name the asset.
    bool bareHashAllowed = false;

    bool operator==(const ChecksumFile&) const = default;
};

/// The newest stable release of a project together with the asset to install.
struct ReleaseSelection {
    Release release;
    ReleaseAsset asset;
    /// Built from owner, repository, tag and asset name. The address GitHub
    /// reports for the asset is not used.
    QUrl downloadUrl;

    /// Only set if GitHub reported no digest for the asset and the release
    /// carries exactly one fitting checksum file.
    std::optional<ChecksumFile> checksumFile;

    /// True if the asset can be verified: through the digest or a checksum file.
    bool hasTrustedHashSource() const { return !asset.sha256.isEmpty() || checksumFile.has_value(); }
};

} // namespace launcher
