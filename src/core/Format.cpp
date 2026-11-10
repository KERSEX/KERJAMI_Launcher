#include "core/Format.h"

#include <QLocale>

using namespace Qt::StringLiterals;

namespace launcher::format {

QString bytes(qint64 count)
{
    return QLocale().formattedDataSize(count, 1, QLocale::DataSizeTraditionalFormat);
}

QString speed(qint64 bytesPerSecond)
{
    return bytes(bytesPerSecond) + u"/s"_s;
}

QString duration(qint64 seconds)
{
    if (seconds < 0)
        return {};
    if (seconds < 60)
        return u"%1 s"_s.arg(seconds);
    if (seconds < 3600)
        return u"%1 min %2 s"_s.arg(seconds / 60).arg(seconds % 60);
    return u"%1 h %2 min"_s.arg(seconds / 3600).arg(seconds % 3600 / 60);
}

} // namespace launcher::format
