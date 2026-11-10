#include "core/JsonFile.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace launcher::JsonFile {

namespace {

/// The launcher's own files are a few kilobytes. Anything far beyond that is
/// not ours and is not read into memory.
constexpr qint64 kMaxFileSize = 16 * 1024 * 1024;

} // namespace

LoadResult load(const QString& path)
{
    QFile file(path);
    if (!file.exists())
        return {Status::NotFound, {}, {}};
    if (!file.open(QIODevice::ReadOnly))
        return {Status::ReadError, {}, file.errorString()};
    if (file.size() > kMaxFileSize)
        return {Status::Corrupt, {}, u"file is larger than %1 bytes"_s.arg(kMaxFileSize)};

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return {Status::Corrupt, {}, parseError.errorString()};
    if (!document.isObject())
        return {Status::Corrupt, {}, u"top-level value is not an object"_s};

    return {Status::Ok, document.object(), {}};
}

bool saveAtomic(const QString& path, const QJsonObject& object, QString* error)
{
    const auto fail = [error](const QString& reason) {
        if (error)
            *error = reason;
        return false;
    };

    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(u"cannot create the target folder"_s);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(file.errorString());

    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        const QString reason = file.errorString();
        file.cancelWriting();
        return fail(reason);
    }
    if (!file.commit())
        return fail(file.errorString());
    return true;
}

QString backupAside(const QString& path, const QString& tag, BackupMode mode)
{
    const QFileInfo info(path);
    if (!info.exists())
        return {};

    const QString stamp = QDateTime::currentDateTime().toString(u"yyyyMMdd-HHmmss"_s);
    const QString stem = info.absolutePath() + u'/' + info.completeBaseName() + u'.' + tag + u'-' + stamp;
    const QString suffix = info.suffix().isEmpty() ? QString() : u'.' + info.suffix();

    // Two backups within the same second must not overwrite each other.
    QString target = stem + suffix;
    for (int attempt = 2; QFileInfo::exists(target) && attempt < 100; ++attempt)
        target = stem + u'-' + QString::number(attempt) + suffix;

    const bool done = mode == BackupMode::Move ? QFile::rename(path, target) : QFile::copy(path, target);
    return done ? target : QString();
}

} // namespace launcher::JsonFile
