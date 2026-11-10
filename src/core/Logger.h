#pragma once

#include <QLoggingCategory>
#include <QString>

namespace launcher {

// One category per subsystem. Use them with qCInfo(lcApp) << "...";
// Debug output is off by default and can be enabled with the environment
// variable QT_LOGGING_RULES="launcher.*.debug=true".
Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcSettings)
Q_DECLARE_LOGGING_CATEGORY(lcNetwork)
Q_DECLARE_LOGGING_CATEGORY(lcRegistry)

/// Writes all Qt log messages of the process to a size-limited, rotating file.
///
/// Line format: 2026-11-09T14:03:22.123 INFO  [settings] message
///
/// A log file must have exactly one writing process. Two processes appending
/// to the same file overwrite each other's entries. The launcher guarantees
/// this by installing the logger only after it holds the instance lock; the
/// updater writes to its own file.
class Logger {
public:
    struct Options {
        /// Folder for the log files. Created if missing.
        QString directory;

        /// "launcher" produces launcher.log, launcher.1.log, launcher.2.log, ...
        QString baseName = QStringLiteral("launcher");

        /// A file is rotated before it would grow beyond this size.
        qint64 maxFileSize = 1024 * 1024;

        /// Number of rotated files kept next to the current one.
        int maxBackupFiles = 4;

        /// Absolute path replaced by "<root>" in every message. The portable
        /// folder often lives below the user profile, and the user name does
        /// not belong in a log that gets shared for support.
        QString redactRoot;
    };

    /// Installs the process-wide message handler. Returns false if the log
    /// file cannot be opened or a logger is already installed.
    static bool install(const Options& options);

    /// Restores the previous message handler and closes the file.
    static void uninstall();

    /// Path of the file currently written to, empty if not installed.
    static QString currentFilePath();
};

} // namespace launcher
