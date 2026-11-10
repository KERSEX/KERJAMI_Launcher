#include "security/ArchiveValidator.h"

#include "github/RegistryParser.h"

#include <QHash>
#include <QSet>

using namespace Qt::StringLiterals;

namespace launcher::ArchiveValidator {

namespace {

constexpr qsizetype kMaxSegmentLength = 128;

bool isForbiddenCharacter(char32_t c)
{
    if (c < 0x20 || c == 0x7F)
        return true;
    switch (c) {
    case U'<':
    case U'>':
    case U':':
    case U'"':
    case U'|':
    case U'?':
    case U'*':
        return true;
    default:
        break;
    }
    // Invisible and direction-changing characters let a name look like another one.
    switch (QChar::category(c)) {
    case QChar::Other_Control:
    case QChar::Other_Format:
    case QChar::Other_Surrogate:
    case QChar::Other_PrivateUse:
    case QChar::Other_NotAssigned:
    case QChar::Separator_Line:
    case QChar::Separator_Paragraph:
        return true;
    default:
        return false;
    }
}

/// Code points of a UTF-16 string; a lone surrogate comes out as itself and is refused.
template <typename F> bool allCodePoints(const QString& text, F&& accept)
{
    for (qsizetype i = 0; i < text.size(); ++i) {
        char32_t c = text.at(i).unicode();
        if (QChar::isHighSurrogate(c) && i + 1 < text.size()
            && QChar::isLowSurrogate(text.at(i + 1).unicode())) {
            c = QChar::surrogateToUcs4(QChar(c), text.at(i + 1));
            ++i;
        }
        if (!accept(c))
            return false;
    }
    return true;
}

QString refuse(QString* reason, const QString& message)
{
    if (reason != nullptr)
        *reason = message;
    return {};
}

} // namespace

QString normalizeEntryPath(const QString& name, const Limits& limits, bool* isDirectory, QString* reason)
{
    QString path = name;
    path.replace(u'\\', u'/');

    if (path.isEmpty())
        return refuse(reason, u"empty name"_s);
    if (path.startsWith(u'/'))
        return refuse(reason, u"absolute path"_s);

    const bool directory = path.endsWith(u'/');
    if (directory)
        path.chop(1);
    if (isDirectory != nullptr)
        *isDirectory = directory;
    if (path.isEmpty())
        return refuse(reason, u"empty name"_s);

    if (path.size() > limits.maxPathLength)
        return refuse(reason, u"path is longer than %1 characters"_s.arg(limits.maxPathLength));

    const QStringList segments = path.split(u'/');
    if (segments.size() - (directory ? 0 : 1) > limits.maxDepth)
        return refuse(reason, u"folders are nested more than %1 levels deep"_s.arg(limits.maxDepth));

    for (const QString& segment : segments) {
        if (segment.isEmpty())
            return refuse(reason, u"empty path segment"_s);
        if (segment == u"."_s || segment == u".."_s)
            return refuse(reason, u"relative path segment \"%1\""_s.arg(segment));
        if (segment.size() > kMaxSegmentLength)
            return refuse(reason, u"a path segment is longer than %1 characters"_s.arg(kMaxSegmentLength));
        // Windows silently drops trailing dots and spaces, so "a." would become "a".
        if (segment.endsWith(u'.') || segment.endsWith(u' '))
            return refuse(reason, u"path segment ends with a dot or space"_s);
        if (RegistryParser::isReservedWindowsName(segment))
            return refuse(reason, u"reserved Windows name \"%1\""_s.arg(segment));
        if (!allCodePoints(segment, [](char32_t c) { return !isForbiddenCharacter(c); }))
            return refuse(reason, u"forbidden character in a path segment"_s);
    }

    return path;
}

ValidationResult validate(const ZipArchive& archive, const Limits& limits, const QString& requiredExecutable)
{
    ValidationResult result;
    const auto fail = [&result](Problem problem, const QString& detail) {
        result.problem = problem;
        result.detail = detail;
        result.items.clear();
        return result;
    };

    const QList<ZipArchive::Entry>& entries = archive.entries();
    if (entries.size() > limits.maxEntries)
        return fail(Problem::TooManyEntries,
            u"%1 entries, at most %2 allowed"_s.arg(entries.size()).arg(limits.maxEntries));

    // Lowercase path -> is it a directory. Windows file systems ignore case.
    QHash<QString, bool> seen;
    QSet<QString> parents; // every folder some entry lies in
    int files = 0;

    for (qsizetype index = 0; index < entries.size(); ++index) {
        const ZipArchive::Entry& entry = entries.at(index);

        if (entry.kind == ZipArchive::EntryKind::Symlink || entry.kind == ZipArchive::EntryKind::Other)
            return fail(Problem::UnsupportedEntry, u"\"%1\" is a link or special file"_s.arg(entry.name));

        bool isDirectory = false;
        QString reason;
        const QString path = normalizeEntryPath(entry.name, limits, &isDirectory, &reason);
        if (path.isEmpty())
            return fail(Problem::UnsafePath, u"\"%1\": %2"_s.arg(entry.name, reason));

        const QString key = path.toLower();
        if (seen.contains(key))
            return fail(Problem::DuplicatePath, u"\"%1\" appears twice"_s.arg(entry.name));
        seen.insert(key, isDirectory);

        // Every folder above must be a folder: remember them for the conflict check.
        for (qsizetype slash = key.indexOf(u'/'); slash >= 0; slash = key.indexOf(u'/', slash + 1))
            parents.insert(key.left(slash));

        if (isDirectory) {
            result.items.append({path, true, 0, int(index)});
            continue;
        }

        ++files;
        if (qint64(entry.size) > limits.maxEntrySize)
            return fail(Problem::TooLarge,
                u"\"%1\" is larger than %2 bytes"_s.arg(entry.name).arg(limits.maxEntrySize));
        result.totalSize += entry.size;
        if (result.totalSize > limits.maxTotalSize)
            return fail(
                Problem::TooLarge, u"the files add up to more than %1 bytes"_s.arg(limits.maxTotalSize));

        if (qint64(entry.size) >= Limits::kRatioFloor
            && qint64(entry.size) > qint64(entry.compressedSize) * limits.maxCompressionRatio)
            return fail(Problem::CompressionBomb,
                u"\"%1\" expands by more than %2 times"_s.arg(entry.name).arg(limits.maxCompressionRatio));

        result.items.append({path, false, qint64(entry.size), int(index)});
    }

    if (files == 0)
        return fail(Problem::Empty, u"the archive contains no files"_s);

    // A name that is a file and also the folder of another entry cannot be created.
    for (auto it = seen.cbegin(); it != seen.cend(); ++it) {
        if (!it.value() && parents.contains(it.key()))
            return fail(
                Problem::ConflictingPath, u"\"%1\" is used as a file and as a folder"_s.arg(it.key()));
    }

    if (!requiredExecutable.isEmpty()) {
        const auto found = seen.constFind(requiredExecutable.toLower());
        if (found == seen.cend() || found.value())
            return fail(
                Problem::MissingExecutable, u"\"%1\" is not in the archive"_s.arg(requiredExecutable));
    }

    return result;
}

} // namespace launcher::ArchiveValidator
