#pragma once

#include <QString>

namespace launcher {

/// How a project is updated. Every project has exactly one mode.
enum class UpdateMode {
    Manual,    ///< The launcher announces a new version; the user decides.
    Automatic, ///< The launcher downloads it and installs it once the project is closed.
};

inline QString updateModeToString(UpdateMode mode)
{
    return mode == UpdateMode::Automatic ? QStringLiteral("automatic") : QStringLiteral("manual");
}

/// Unknown text counts as manual: a damaged setting never switches updates on.
inline UpdateMode updateModeFromString(const QString& text)
{
    return text == QStringLiteral("automatic") ? UpdateMode::Automatic : UpdateMode::Manual;
}

/// What the user chose for one project. Stored by project id, independent of
/// the installed version and of the registry.
struct ProjectSettings {
    UpdateMode updateMode = UpdateMode::Manual;

    bool operator==(const ProjectSettings&) const = default;
};

} // namespace launcher
