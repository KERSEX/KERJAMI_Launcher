#pragma once

#include "installer/InstalledProjects.h"
#include "installer/LibraryLayout.h"

#include <QString>

namespace launcher {

enum class StartError {
    None,
    Busy,           ///< The project is being installed or updated.
    NotInstalled,   ///< The project is not in the list of installed projects.
    AlreadyRunning, ///< The project is running; it is not started a second time.
    FilesMissing,   ///< The start file is gone from the library.
    ProtectedPath,  ///< The start file lies outside the project's current/ folder, or a link is in the way.
    StartFailed,    ///< Windows refused to start the program.
};

struct StartResult {
    StartError error = StartError::None;
    QString detail; ///< Technical detail for the log.
    qint64 processId = 0;

    bool ok() const { return error == StartError::None; }
};

/// The file to start and the folder to start it in, after all checks.
struct StartTarget {
    StartError error = StartError::None;
    QString detail; ///< Technical detail for the log.
    QString program;
    QString workingDirectory;

    bool ok() const { return error == StartError::None; }
};

/// Decides what may be started for a project: only the start file recorded at
/// installation, only from inside the project's current/ folder, and only if
/// neither the folder nor the file is a link.
StartTarget resolveStartTarget(
    const LibraryLayout& layout, const InstalledProjects& installed, const QString& projectId);

/// Starts an installed project on its own, without watching it: the program
/// keeps running if the launcher is closed. The ProcessManager is the way to
/// start a project that the launcher should keep track of.
StartResult startProject(const LibraryLayout& layout, InstalledProjects& installed, const QString& projectId);

} // namespace launcher
