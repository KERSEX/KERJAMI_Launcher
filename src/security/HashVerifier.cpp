#include "security/HashVerifier.h"

#include <QCryptographicHash>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher::HashVerifier {

namespace {

constexpr qint64 kChunk = 1024 * 1024;
constexpr qsizetype kMaxChecksumFileBytes = 64 * 1024;
constexpr qsizetype kMaxLines = 2000;

bool isHexDigit(QChar c)
{
    return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
}

} // namespace

bool isValidSha256(const QString& hex)
{
    return hex.size() == 64 && std::all_of(hex.cbegin(), hex.cend(), isHexDigit);
}

QString sha256File(
    const QString& path, QString* error, const std::atomic<bool>* cancel, const Progress& progress)
{
    const auto fail = [error](const QString& message) {
        if (error != nullptr)
            *error = message;
        return QString();
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(file.errorString());

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(kChunk, Qt::Uninitialized);
    qint64 processed = 0;
    for (;;) {
        if (cancel != nullptr && cancel->load())
            return fail(u"cancelled"_s);
        const qint64 count = file.read(buffer.data(), buffer.size());
        if (count < 0)
            return fail(file.errorString());
        if (count == 0)
            break;
        hash.addData(QByteArrayView(buffer.constData(), count));
        processed += count;
        if (progress)
            progress(processed);
    }
    if (error != nullptr)
        error->clear();
    return QString::fromLatin1(hash.result().toHex());
}

VerifyResult verifySha256(
    const QString& path, const QString& expected, const std::atomic<bool>* cancel, const Progress& progress)
{
    VerifyResult result;
    const QString wanted = expected.trimmed();
    if (wanted.isEmpty()) {
        result.status = Status::NoExpectedHash;
        return result;
    }
    if (!isValidSha256(wanted)) {
        result.status = Status::InvalidExpectedHash;
        return result;
    }

    QString error;
    result.actual = sha256File(path, &error, cancel, progress);
    if (result.actual.isEmpty()) {
        const bool cancelled = cancel != nullptr && cancel->load();
        result.status = cancelled ? Status::Cancelled : Status::FileError;
        result.detail = error;
        return result;
    }

    result.status = result.actual == wanted.toLower() ? Status::Match : Status::Mismatch;
    return result;
}

QString findInChecksumFile(const QByteArray& content, const QString& fileName, bool allowBareHash)
{
    if (content.size() > kMaxChecksumFileBytes || fileName.isEmpty())
        return {};

    QString found;
    const QList<QByteArray> lines = content.split('\n');
    if (lines.size() > kMaxLines)
        return {};

    for (const QByteArray& raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;

        const qsizetype space = line.indexOf(QRegularExpression(u"\\s"_s));
        const QString hash = space < 0 ? line : line.left(space);
        if (!isValidSha256(hash))
            return {}; // a line that is not a checksum line: the file is not what we think it is

        QString name = space < 0 ? QString() : line.mid(space).trimmed();
        if (name.startsWith(u'*')) // binary mode marker of sha256sum
            name.remove(0, 1);
        if (name.startsWith(u"./"_s))
            name.remove(0, 2);

        const bool matches = name.isEmpty() ? allowBareHash : name == fileName;
        if (!matches)
            continue;
        const QString lower = hash.toLower();
        if (!found.isEmpty() && found != lower)
            return {}; // two different answers for the same file
        found = lower;
    }
    return found;
}

} // namespace launcher::HashVerifier
