#pragma once

#include "installer/InstalledProjects.h"
#include "installer/LibraryLayout.h"

#include <QString>

namespace launcher {

enum class StartError {
    None,
    Busy,          ///< The project is being installed or updated.
    NotInstalled,  ///< The project is not in the list of installed projects.
    FilesMissing,  ///< The start file is gone from the library.
    ProtectedPath, ///< The start file lies outside the project's current/ folder, or a link is in the way.
    StartFailed,   ///< Windows refused to start the program.
};

struct StartResult {
    StartError error = StartError::None;
    QString detail; ///< Technical detail for the log.
    qint64 processId = 0;

    bool ok() const { return error == StartError::None; }
};

/// Starts an installed project.
///
/// Only the start file recorded at installation is started, and only from
/// inside the project's current/ folder. The program runs on its own: it
/// keeps running if the launcher is closed.
StartResult startProject(const LibraryLayout& layout, InstalledProjects& installed, const QString& projectId);

} // namespace launcher
