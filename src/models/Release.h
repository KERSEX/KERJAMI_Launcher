#pragma once

#include "core/SemVer.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>

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

/// The newest stable release of a project together with the asset to install.
struct ReleaseSelection {
    Release release;
    ReleaseAsset asset;
    /// Built from owner, repository, tag and asset name. The address GitHub
    /// reports for the asset is not used.
    QUrl downloadUrl;
};

} // namespace launcher
