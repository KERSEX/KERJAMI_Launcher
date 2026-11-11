#pragma once

#include <QString>
#include <QStringList>

namespace launcher {

/// Release channels a project can follow. Version 1 supports stable releases only.
enum class ReleaseChannel {
    Stable,
};

/// A project approved in the registry.
///
/// Instances come out of RegistryParser, which has validated every field.
/// owner and repository are only used to build GitHub addresses; the registry
/// never supplies a download address.
struct Project {
    QString id;   ///< Unique key, also used for per-project settings.
    QString name; ///< Display name.
    QString description;
    QString owner;      ///< GitHub account or organisation.
    QString repository; ///< GitHub repository name.
    QString category;
    QStringList tags;
    bool enabled = true; ///< Only enabled projects are shown.
    bool featured = false;
    ReleaseChannel releaseChannel = ReleaseChannel::Stable;
    QString tagPrefix; ///< Text before the version in a release tag, e.g. "KERS_SubsystemsV". Empty: the tag
                       ///< is the version, with an optional "v".
    QString assetPattern;     ///< File name of the ZIP release asset; "*" matches any characters.
    QString executable;       ///< Path of the start file inside the archive, with "/" separators.
    QString installDirectory; ///< Folder name below the project library.

    bool operator==(const Project&) const = default;
};

} // namespace launcher
