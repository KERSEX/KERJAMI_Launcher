#pragma once

#include "updater/UpdateManifest.h"

#include <QString>
#include <QStringList>

#include <functional>

namespace launcher {

/// Everything the replacement of the launcher's program files needs to know.
struct TransactionPlan {
    QString rootDir;     ///< Absolute path of the launcher's folder.
    QString stagingDir;  ///< Absolute; the new files lie here at their relative paths.
    QString backupDir;   ///< Absolute; replaced files are moved here. Must be inside rootDir.
    QString journalFile; ///< Absolute; written before the first file is touched.
    QString fromVersion;
    QString toVersion;
    UpdateManifest manifest;

    /// For tests only: asked after every step; returning true makes apply() stop
    /// on the spot, without rolling back, as if the process had been killed.
    std::function<bool(int step)> simulateCrashAfter;
};

enum class TransactionOutcome {
    Success,        ///< The new files are in place (or already were).
    NothingToDo,    ///< recover(): there was no interrupted update.
    NotApplied,     ///< Refused before anything was changed.
    RolledBack,     ///< A step failed; the earlier state was restored.
    RollbackFailed, ///< A step failed and the earlier state could not be fully restored.
    Interrupted,    ///< simulateCrashAfter stopped apply(). Only tests see this.
};

struct TransactionResult {
    TransactionOutcome outcome = TransactionOutcome::NothingToDo;
    QString detail;     ///< Technical detail for the log.
    QString failedPath; ///< The path a failed step was working on.
    int filesChanged = 0;

    bool ok() const { return outcome == TransactionOutcome::Success; }
};

/// Replaces program files all or nothing, in a way that survives being killed.
///
/// Steps: check everything (the staged files against the manifest's hashes,
/// every target path against the rules for paths) → write a journal → move each
/// file that is to be replaced into the backup folder and move the new one in
/// its place → check the placed files. The launcher program is replaced last:
/// as long as it is the old one, the old version can still be started.
///
/// A failing step restores the earlier state and reports RolledBack. If the
/// process dies in the middle, the journal stays on disk and recover(), called
/// by the next launcher start, decides from the files themselves: all new
/// means the update was finished, anything else is rolled back.
///
/// Only paths from the manifest are touched. Nothing outside rootDir, nothing
/// in the user's data folders, no link is ever followed.
namespace UpdateTransaction {

TransactionResult apply(const TransactionPlan& plan);

/// Finishes or rolls back an update that was interrupted. NothingToDo without a journal.
TransactionResult recover(const QString& rootDir, const QString& journalFile);

} // namespace UpdateTransaction

} // namespace launcher
