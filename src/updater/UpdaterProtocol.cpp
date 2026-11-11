#include "updater/UpdaterProtocol.h"

using namespace Qt::StringLiterals;

namespace launcher {

QString statusToString(UpdateReport::Status status)
{
    switch (status) {
    case UpdateReport::Status::Success:
        return u"success"_s;
    case UpdateReport::Status::RolledBack:
        return u"rolled_back"_s;
    case UpdateReport::Status::NotApplied:
        return u"not_applied"_s;
    case UpdateReport::Status::Failed:
        return u"failed"_s;
    }
    return u"failed"_s;
}

namespace {

std::optional<UpdateReport::Status> statusFromString(const QString& text)
{
    for (const auto status : {UpdateReport::Status::Success, UpdateReport::Status::RolledBack,
             UpdateReport::Status::NotApplied, UpdateReport::Status::Failed}) {
        if (statusToString(status) == text)
            return status;
    }
    return std::nullopt;
}

} // namespace

QJsonObject UpdaterJob::toJson() const
{
    return {{u"schema_version"_s, kSchemaVersion}, {u"root_dir"_s, rootDir}, {u"staging_dir"_s, stagingDir},
        {u"from_version"_s, fromVersion}, {u"to_version"_s, toVersion}, {u"launcher_file"_s, launcherFile},
        {u"product"_s, product}, {u"launcher_process_id"_s, static_cast<double>(launcherProcessId)},
        {u"wait_timeout_ms"_s, waitTimeoutMs}, {u"start_launcher_after"_s, startLauncherAfter}};
}

std::optional<UpdaterJob> UpdaterJob::fromJson(const QJsonObject& object)
{
    if (object.value(u"schema_version"_s).toInt() != kSchemaVersion)
        return std::nullopt;

    UpdaterJob job;
    job.rootDir = object.value(u"root_dir"_s).toString();
    job.stagingDir = object.value(u"staging_dir"_s).toString();
    job.fromVersion = object.value(u"from_version"_s).toString();
    job.toVersion = object.value(u"to_version"_s).toString();
    job.launcherFile = object.value(u"launcher_file"_s).toString();
    job.product = object.value(u"product"_s).toString();
    job.launcherProcessId = static_cast<qint64>(object.value(u"launcher_process_id"_s).toDouble());
    job.waitTimeoutMs = object.value(u"wait_timeout_ms"_s).toInt(kDefaultWaitTimeoutMs);
    job.startLauncherAfter = object.value(u"start_launcher_after"_s).toBool(true);

    const bool usable = !job.rootDir.isEmpty() && !job.stagingDir.isEmpty() && !job.launcherFile.isEmpty()
        && !job.product.isEmpty() && !job.toVersion.isEmpty() && job.launcherProcessId >= 0
        && job.waitTimeoutMs >= 0 && job.waitTimeoutMs <= 10 * 60 * 1000;
    if (!usable)
        return std::nullopt;
    return job;
}

QJsonObject UpdateReport::toJson() const
{
    return {{u"schema_version"_s, 1}, {u"status"_s, statusToString(status)}, {u"from_version"_s, fromVersion},
        {u"to_version"_s, toVersion}, {u"detail"_s, detail}, {u"failed_path"_s, failedPath},
        {u"backup_dir"_s, backupDir}, {u"launcher_started"_s, launcherStarted},
        {u"finished_at"_s, finishedAt.isValid() ? finishedAt.toUTC().toString(Qt::ISODate) : QString()}};
}

std::optional<UpdateReport> UpdateReport::fromJson(const QJsonObject& object)
{
    const std::optional<Status> status = statusFromString(object.value(u"status"_s).toString());
    if (!status)
        return std::nullopt;

    UpdateReport report;
    report.status = *status;
    report.fromVersion = object.value(u"from_version"_s).toString();
    report.toVersion = object.value(u"to_version"_s).toString();
    report.detail = object.value(u"detail"_s).toString();
    report.failedPath = object.value(u"failed_path"_s).toString();
    report.backupDir = object.value(u"backup_dir"_s).toString();
    report.launcherStarted = object.value(u"launcher_started"_s).toBool();
    report.finishedAt = QDateTime::fromString(object.value(u"finished_at"_s).toString(), Qt::ISODate).toUTC();
    return report;
}

} // namespace launcher
