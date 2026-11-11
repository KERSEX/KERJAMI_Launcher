#include "updater/UpdateTransaction.h"

#include "core/JsonFile.h"
#include "core/Logger.h"
#include "core/PathManager.h"
#include "installer/FileOps.h"
#include "security/HashVerifier.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher::UpdateTransaction {

namespace {

constexpr int kJournalVersion = 1;

TransactionResult result(TransactionOutcome outcome, const QString& detail, const QString& path = {})
{
    TransactionResult r;
    r.outcome = outcome;
    r.detail = detail;
    r.failedPath = path;
    return r;
}

QString join(const QString& folder, const QString& relative)
{
    return QDir::cleanPath(folder + u'/' + relative);
}

/// A path from a manifest or journal is usable only if it is a plain relative
/// file path outside the protected folders, its target lies inside the root,
/// and no link lies between the root and the file.
bool safeTarget(const QString& root, const QString& relative, QString* why)
{
    QString reason;
    if (UpdateManifestFormat::checkedPath(relative, &reason) != relative) {
        *why = u"\"%1\": %2"_s.arg(relative, reason);
        return false;
    }
    const QString target = join(root, relative);
    if (!PathManager::isInside(root, target)) {
        *why = u"\"%1\" lies outside the launcher's folder"_s.arg(relative);
        return false;
    }
    for (QString dir = QFileInfo(target).absolutePath(); dir.size() > root.size();
        dir = QFileInfo(dir).absolutePath()) {
        if (FileOps::isLink(dir)) {
            *why = u"a link lies in the way of \"%1\""_s.arg(relative);
            return false;
        }
    }
    const QFileInfo info(target);
    if (FileOps::isLink(target) || (info.exists() && !info.isFile())) {
        *why = u"\"%1\" exists and is not a plain file"_s.arg(relative);
        return false;
    }
    return true;
}

QString sha(const QString& path)
{
    return HashVerifier::sha256File(path);
}

struct Change {
    ManifestFile file;
    QString target;
    QString staged;
    QString backup;
    bool hadOld = false;
    bool backedUp = false;
    bool placed = false;
};

struct Removal {
    QString path;
    QString target;
    QString backup;
    bool backedUp = false;
};

QJsonObject journalOf(
    const TransactionPlan& plan, const QList<Change>& changes, const QList<Removal>& removals)
{
    QJsonArray files;
    for (const Change& change : changes)
        files.append(QJsonObject {
            {u"path"_s, change.file.path}, {u"sha256"_s, change.file.sha256}, {u"had_old"_s, change.hadOld}});
    QJsonArray removed;
    for (const Removal& removal : removals)
        removed.append(QJsonObject {{u"path"_s, removal.path}});
    QDir root(plan.rootDir);
    return {{u"schema_version"_s, kJournalVersion}, {u"state"_s, u"swapping"_s},
        {u"from_version"_s, plan.fromVersion}, {u"to_version"_s, plan.toVersion},
        {u"backup_dir"_s, root.relativeFilePath(plan.backupDir)}, {u"files"_s, files},
        {u"remove"_s, removed}};
}

/// Removes the empty folders above a file the update had added, up to (not including) the root.
void removeEmptyParents(const QString& root, const QString& file)
{
    for (QString dir = QFileInfo(file).absolutePath();
        dir.size() > root.size() && PathManager::isInside(root, dir); dir = QFileInfo(dir).absolutePath()) {
        if (!QDir().rmdir(dir))
            return; // not empty, or not ours to remove
    }
}

/// Undoes what was done, newest step first. Returns false if something could not be restored.
bool rollBack(
    QList<Change>& changes, QList<Removal>& removals, const QStringList& createdDirs, QString* firstProblem)
{
    bool complete = true;
    const auto problem = [&](const QString& path) {
        complete = false;
        if (firstProblem != nullptr && firstProblem->isEmpty())
            *firstProblem = path;
    };

    for (qsizetype i = removals.size() - 1; i >= 0; --i) {
        Removal& removal = removals[i];
        if (removal.backedUp && !FileOps::renameWithRetry(removal.backup, removal.target))
            problem(removal.path);
    }
    for (qsizetype i = changes.size() - 1; i >= 0; --i) {
        Change& change = changes[i];
        if (change.placed && !QFile::remove(change.target)) {
            // The new file cannot be removed (locked): the old one cannot go back in its place.
            problem(change.file.path);
            continue;
        }
        if (change.backedUp && !FileOps::renameWithRetry(change.backup, change.target))
            problem(change.file.path);
    }
    for (qsizetype i = createdDirs.size() - 1; i >= 0; --i)
        QDir().rmdir(createdDirs.at(i)); // only succeeds when empty
    return complete;
}

} // namespace

TransactionResult apply(const TransactionPlan& plan)
{
    const QString root = QDir::cleanPath(plan.rootDir);
    const QString staging = QDir::cleanPath(plan.stagingDir);
    const QString backupDir = QDir::cleanPath(plan.backupDir);

    if (!QFileInfo(root).isDir() || !QFileInfo(staging).isDir())
        return result(
            TransactionOutcome::NotApplied, u"the launcher's folder or the staging folder is missing"_s);
    if (!PathManager::isInside(root, backupDir) || !PathManager::isInside(root, staging)
        || !PathManager::isInside(root, QDir::cleanPath(plan.journalFile)))
        return result(TransactionOutcome::NotApplied,
            u"backup, staging or journal lie outside the launcher's folder"_s);
    if (QFileInfo::exists(backupDir) && !QDir(backupDir).isEmpty())
        return result(TransactionOutcome::NotApplied, u"the backup folder is not empty"_s);

    // --- 1. check everything before a single file is touched -----------------------
    QList<Change> changes;
    for (const ManifestFile& file : plan.manifest.files) {
        QString why;
        if (!safeTarget(root, file.path, &why))
            return result(TransactionOutcome::NotApplied, why, file.path);

        Change change;
        change.file = file;
        change.target = join(root, file.path);
        change.staged = join(staging, file.path);
        change.backup = join(backupDir, file.path);

        const QFileInfo staged(change.staged);
        if (!staged.isFile() || FileOps::isLink(change.staged) || staged.size() != file.size
            || sha(change.staged) != file.sha256)
            return result(TransactionOutcome::NotApplied,
                u"the staged file \"%1\" is missing or does not match the manifest"_s.arg(file.path),
                file.path);

        change.hadOld = QFileInfo::exists(change.target);
        if (change.hadOld && sha(change.target) == file.sha256)
            continue; // already the new version
        changes.append(change);
    }

    QList<Removal> removals;
    for (const QString& path : plan.manifest.remove) {
        QString why;
        if (!safeTarget(root, path, &why))
            return result(TransactionOutcome::NotApplied, why, path);
        if (QFileInfo::exists(join(root, path)))
            removals.append({path, join(root, path), join(backupDir, path), false});
    }

    if (changes.isEmpty() && removals.isEmpty())
        return result(TransactionOutcome::Success, u"all files are in place already"_s);

    // The launcher program last: until then the old version can still be started.
    const QString launcher = plan.manifest.launcher.toLower();
    const auto rank = [&](const Change& change) { return change.file.path.toLower() == launcher ? 1 : 0; };
    std::stable_sort(
        changes.begin(), changes.end(), [&](const Change& a, const Change& b) { return rank(a) < rank(b); });

    // --- 2. journal ----------------------------------------------------------------
    if (!QDir().mkpath(backupDir))
        return result(TransactionOutcome::NotApplied, u"the backup folder cannot be created"_s);
    QString error;
    if (!JsonFile::saveAtomic(plan.journalFile, journalOf(plan, changes, removals), &error))
        return result(TransactionOutcome::NotApplied, u"the journal cannot be written: %1"_s.arg(error));

    // --- 3. replace ------------------------------------------------------------------
    QStringList createdDirs;
    int steps = 0;
    const auto fail = [&](const QString& detail, const QString& path) {
        QString firstProblem;
        const bool restored = rollBack(changes, removals, createdDirs, &firstProblem);
        if (restored) {
            QFile::remove(plan.journalFile);
            FileOps::removeTree(backupDir);
            return result(TransactionOutcome::RolledBack, detail, path);
        }
        // The journal stays: the next start of the launcher tries again (recover).
        return result(TransactionOutcome::RollbackFailed,
            u"%1; and \"%2\" could not be restored"_s.arg(detail, firstProblem), path);
    };

    for (Change& change : changes) {
        // Folders this step has to create are noted, so that a rollback removes them again.
        QStringList missing;
        for (QString dir = QFileInfo(change.target).absolutePath(); !QFileInfo::exists(dir);
            dir = QFileInfo(dir).absolutePath())
            missing.prepend(dir);
        if (!QDir().mkpath(QFileInfo(change.target).absolutePath()))
            return fail(u"the folder of \"%1\" cannot be created"_s.arg(change.file.path), change.file.path);
        createdDirs += missing;

        if (change.hadOld) {
            if (!QDir().mkpath(QFileInfo(change.backup).absolutePath()))
                return fail(
                    u"the backup of \"%1\" cannot be prepared"_s.arg(change.file.path), change.file.path);
            if (!FileOps::renameWithRetry(change.target, change.backup))
                return fail(
                    u"\"%1\" is in use and cannot be replaced"_s.arg(change.file.path), change.file.path);
            change.backedUp = true;
        }
        if (!FileOps::renameWithRetry(change.staged, change.target))
            return fail(
                u"the new \"%1\" cannot be moved into place"_s.arg(change.file.path), change.file.path);
        change.placed = true;

        if (plan.simulateCrashAfter && plan.simulateCrashAfter(++steps))
            return result(TransactionOutcome::Interrupted, u"stopped after step %1"_s.arg(steps));
    }

    for (Removal& removal : removals) {
        if (!QDir().mkpath(QFileInfo(removal.backup).absolutePath()))
            return fail(u"the backup of \"%1\" cannot be prepared"_s.arg(removal.path), removal.path);
        if (!FileOps::renameWithRetry(removal.target, removal.backup))
            return fail(u"\"%1\" is in use and cannot be removed"_s.arg(removal.path), removal.path);
        removal.backedUp = true;
    }

    // --- 4. check what is in place -----------------------------------------------------
    for (const Change& change : std::as_const(changes)) {
        if (sha(change.target) != change.file.sha256)
            return fail(u"\"%1\" does not match the manifest after it was moved"_s.arg(change.file.path),
                change.file.path);
    }

    // --- 5. done --------------------------------------------------------------------
    QFile::remove(plan.journalFile);
    TransactionResult done =
        result(TransactionOutcome::Success, u"%1 file(s) replaced"_s.arg(changes.size()));
    done.filesChanged = static_cast<int>(changes.size() + removals.size());
    return done;
}

TransactionResult recover(const QString& rootDirectory, const QString& journalFile)
{
    const QString root = QDir::cleanPath(rootDirectory);
    const JsonFile::LoadResult journal = JsonFile::load(journalFile);
    if (journal.status == JsonFile::Status::NotFound)
        return result(TransactionOutcome::NothingToDo, {});
    if (journal.status != JsonFile::Status::Ok) {
        // A journal that cannot be read cannot be judged; it must not block every later start.
        JsonFile::backupAside(journalFile, u"unreadable"_s, JsonFile::BackupMode::Move);
        return result(
            TransactionOutcome::NotApplied, u"the journal of an interrupted update is not readable"_s);
    }

    const QJsonObject object = journal.object;
    if (object.value(u"schema_version"_s).toInt() != kJournalVersion
        || object.value(u"state"_s).toString() != u"swapping"_s) {
        JsonFile::backupAside(journalFile, u"unknown"_s, JsonFile::BackupMode::Move);
        return result(
            TransactionOutcome::NotApplied, u"the journal of an interrupted update has an unknown form"_s);
    }

    const QString backupDir = join(root, object.value(u"backup_dir"_s).toString());
    if (!PathManager::isInside(root, backupDir)) {
        JsonFile::backupAside(journalFile, u"unsafe"_s, JsonFile::BackupMode::Move);
        return result(TransactionOutcome::NotApplied,
            u"the journal names a backup folder outside the launcher's folder"_s);
    }

    struct Entry {
        QString path;
        QString sha256;
        bool hadOld = false;
        QString target;
        QString backup;
    };
    QList<Entry> files;
    for (const QJsonValue& value : object.value(u"files"_s).toArray()) {
        const QJsonObject entry = value.toObject();
        Entry item;
        item.path = entry.value(u"path"_s).toString();
        item.sha256 = entry.value(u"sha256"_s).toString();
        item.hadOld = entry.value(u"had_old"_s).toBool();
        QString why;
        if (!safeTarget(root, item.path, &why) || !HashVerifier::isValidSha256(item.sha256)) {
            JsonFile::backupAside(journalFile, u"unsafe"_s, JsonFile::BackupMode::Move);
            return result(
                TransactionOutcome::NotApplied, u"the journal lists an unusable path: %1"_s.arg(why));
        }
        item.target = join(root, item.path);
        item.backup = join(backupDir, item.path);
        files.append(item);
    }
    QList<Entry> removed;
    for (const QJsonValue& value : object.value(u"remove"_s).toArray()) {
        Entry item;
        item.path = value.toObject().value(u"path"_s).toString();
        QString why;
        if (!safeTarget(root, item.path, &why)) {
            JsonFile::backupAside(journalFile, u"unsafe"_s, JsonFile::BackupMode::Move);
            return result(
                TransactionOutcome::NotApplied, u"the journal lists an unusable path: %1"_s.arg(why));
        }
        item.target = join(root, item.path);
        item.backup = join(backupDir, item.path);
        removed.append(item);
    }

    // The files themselves tell how far the update got. The launcher program is
    // replaced last, so "all files new" means the update was complete.
    bool allNew = true;
    for (const Entry& item : std::as_const(files)) {
        if (!QFileInfo::exists(item.target) || sha(item.target) != item.sha256) {
            allNew = false;
            break;
        }
    }
    for (const Entry& item : std::as_const(removed)) {
        if (QFileInfo::exists(item.target))
            allNew = false;
    }
    if (allNew) {
        QFile::remove(journalFile);
        return result(TransactionOutcome::Success, u"the interrupted update had been completed"_s);
    }

    // Otherwise back to the old state.
    bool complete = true;
    QString firstProblem;
    const auto problem = [&](const QString& path) {
        complete = false;
        if (firstProblem.isEmpty())
            firstProblem = path;
    };
    for (const Entry& item : std::as_const(removed)) {
        if (QFileInfo::exists(item.backup) && !QFileInfo::exists(item.target)
            && !FileOps::renameWithRetry(item.backup, item.target))
            problem(item.path);
    }
    for (qsizetype i = files.size() - 1; i >= 0; --i) {
        const Entry& item = files.at(i);
        const bool hasBackup = QFileInfo::exists(item.backup);
        const bool targetIsNew = QFileInfo::exists(item.target) && sha(item.target) == item.sha256;
        if (hasBackup) {
            if (QFileInfo::exists(item.target) && !QFile::remove(item.target)) {
                problem(item.path);
                continue;
            }
            if (!FileOps::renameWithRetry(item.backup, item.target))
                problem(item.path);
        } else if (!item.hadOld && targetIsNew) {
            if (!QFile::remove(item.target))
                problem(item.path); // a file the update added
            else
                removeEmptyParents(root, item.target);
        }
        // had an old file but no backup: the update never reached it, the old file is still there.
    }

    if (!complete)
        return result(TransactionOutcome::RollbackFailed,
            u"the interrupted update could not be rolled back completely"_s, firstProblem);
    QFile::remove(journalFile);
    FileOps::removeTree(backupDir);
    return result(TransactionOutcome::RolledBack, u"the interrupted update was rolled back"_s);
}

} // namespace launcher::UpdateTransaction
