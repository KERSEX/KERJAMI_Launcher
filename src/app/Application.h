#pragma once

#include <QApplication>
#include <QCoreApplication>

namespace launcher {

/// Owns the startup sequence: portable folder check, logging, single-instance
/// lock, settings and the main window.
class Application {
    Q_DECLARE_TR_FUNCTIONS(launcher::Application)

public:
    enum ExitCode {
        ExitOk = 0,
        ExitStorageUnavailable = 2, ///< The portable folder is missing or not writable.
    };

    Application(int& argc, char** argv);

    /// Runs the launcher until the main window is closed.
    int run();

private:
    QApplication m_app;
};

} // namespace launcher
