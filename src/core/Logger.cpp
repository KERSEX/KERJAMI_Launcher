#include "core/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace launcher {

Q_LOGGING_CATEGORY(lcApp, "launcher.app", QtInfoMsg)
Q_LOGGING_CATEGORY(lcSettings, "launcher.settings", QtInfoMsg)
Q_LOGGING_CATEGORY(lcNetwork, "launcher.network", QtInfoMsg)
Q_LOGGING_CATEGORY(lcRegistry, "launcher.registry", QtInfoMsg)

namespace {

constexpr qint64 kMinFileSize = 1024;
constexpr qsizetype kMaxMessageLength = 8 * 1024;

struct LogState {
    QMutex mutex;
    bool installed = false;
    Logger::Options options;
    QString redactForward; ///< Root with forward slashes, empty if redaction is off.
    QString redactNative;  ///< Root with backslashes.
    QFile file;
    QtMessageHandler previousHandler = nullptr;
};

/// Intentionally never destroyed: messages can still arrive while static
/// objects are torn down at process exit.
LogState& state()
{
    static LogState* instance = new LogState;
    return *instance;
}

QString filePath(const Logger::Options& options, int index)
{
    const QString suffix = index == 0 ? u".log"_s : u".%1.log"_s.arg(index);
    return options.directory + u'/' + options.baseName + suffix;
}

bool openCurrent(LogState& s, QIODevice::OpenMode mode)
{
    s.file.setFileName(filePath(s.options, 0));
    return s.file.open(QIODevice::WriteOnly | mode);
}

/// Shifts launcher.log -> launcher.1.log -> ... and drops the oldest file.
void rotate(LogState& s)
{
    s.file.close();

    const int backups = s.options.maxBackupFiles;
    if (backups > 0) {
        QFile::remove(filePath(s.options, backups));
        for (int index = backups - 1; index >= 0; --index)
            QFile::rename(filePath(s.options, index), filePath(s.options, index + 1));
    }

    // Truncating covers two cases: no backups are kept, or the rename failed
    // because another program holds the file. The size limit holds either way.
    openCurrent(s, QIODevice::Truncate);
}

QLatin1StringView levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "DEBUG"_L1;
    case QtInfoMsg:
        return "INFO "_L1;
    case QtWarningMsg:
        return "WARN "_L1;
    case QtCriticalMsg:
        return "ERROR"_L1;
    case QtFatalMsg:
        return "FATAL"_L1;
    }
    return "?????"_L1;
}

QByteArray formatLine(
    const LogState& s, QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    QString text = message;
    if (!s.redactForward.isEmpty()) {
        text.replace(s.redactForward, u"<root>"_s, Qt::CaseInsensitive);
        text.replace(s.redactNative, u"<root>"_s, Qt::CaseInsensitive);
    }
    if (text.size() > kMaxMessageLength) {
        text.truncate(kMaxMessageLength);
        text += u" [truncated]"_s;
    }
    // Continuation lines are indented so every entry still starts with a timestamp.
    text.replace(u'\n', u"\n    "_s);

    QString category = QString::fromLatin1(context.category ? context.category : "default");
    if (category.startsWith("launcher."_L1))
        category.remove(0, 9);

    const QString line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + u' ' + levelName(type)
        + u" ["_s + category + u"] "_s + text + u'\n';
    return line.toUtf8();
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    LogState& s = state();
    QtMessageHandler previous = nullptr;
    {
        QMutexLocker locker(&s.mutex);
        previous = s.previousHandler;
        if (s.installed) {
            // A failed rotation leaves the file closed; try again with the next message.
            if (!s.file.isOpen())
                openCurrent(s, QIODevice::Append);

            const QByteArray line = formatLine(s, type, context, message);
            if (s.file.isOpen() && s.file.size() + line.size() > s.options.maxFileSize)
                rotate(s);
            if (s.file.isOpen()) {
                s.file.write(line);
                s.file.flush(); // entries must survive a crash
            }
        }
    }

    // Keeps debugger output, the Qt Test log and the abort on qFatal working.
    if (previous)
        previous(type, context, message);
}

} // namespace

bool Logger::install(const Options& options)
{
    LogState& s = state();
    QMutexLocker locker(&s.mutex);
    if (s.installed)
        return false;

    s.options = options;
    s.options.maxFileSize = std::max(options.maxFileSize, kMinFileSize);
    s.options.maxBackupFiles = std::max(options.maxBackupFiles, 0);

    // A bare drive root such as "E:/" carries no private information, and
    // replacing a two-character string would mangle unrelated text.
    const QString root = options.redactRoot.isEmpty() ? QString() : QDir::cleanPath(options.redactRoot);
    if (root.size() > 3) {
        s.redactForward = root;
        s.redactNative = QDir::toNativeSeparators(root);
    } else {
        s.redactForward.clear();
        s.redactNative.clear();
    }

    if (!QDir().mkpath(s.options.directory) || !openCurrent(s, QIODevice::Append))
        return false;

    s.installed = true;
    s.previousHandler = qInstallMessageHandler(messageHandler);
    return true;
}

void Logger::uninstall()
{
    LogState& s = state();
    QMutexLocker locker(&s.mutex);
    if (!s.installed)
        return;

    qInstallMessageHandler(s.previousHandler);
    s.previousHandler = nullptr;
    s.file.close();
    s.installed = false;
}

QString Logger::currentFilePath()
{
    LogState& s = state();
    QMutexLocker locker(&s.mutex);
    return s.installed ? filePath(s.options, 0) : QString();
}

} // namespace launcher
