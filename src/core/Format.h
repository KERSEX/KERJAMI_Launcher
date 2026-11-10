#pragma once

#include <QString>

/// Text for numbers the user reads: sizes, speeds and durations.
namespace launcher::format {

/// "56,2 MB" (decimal separator and unit labels follow the default locale).
/// Sizes count in steps of 1024.
QString bytes(qint64 count);

/// "3,1 MB/s"
QString speed(qint64 bytesPerSecond);

/// "12 s", "3 min 5 s", "1 h 4 min". Negative values give an empty text.
QString duration(qint64 seconds);

} // namespace launcher::format
