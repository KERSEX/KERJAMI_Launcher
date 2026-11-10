#pragma once

#include <QtGlobal>

namespace launcher {

/// Download speed as a smoothed average, so that the display does not jump.
///
/// Fed with the number of bytes received and the time that has passed. Takes
/// no clock of its own, which keeps it testable.
class TransferRate {
public:
    /// Samples closer together than this are collected until enough time has passed.
    static constexpr qint64 kMinIntervalMs = 250;

    /// Share of a new measurement in the average.
    static constexpr double kWeight = 0.3;

    /// Records that @p bytes had arrived @p timeMs milliseconds after the start.
    /// A byte count that goes backwards (the transfer began again) restarts the measurement.
    void addSample(qint64 timeMs, qint64 bytes);

    /// Bytes per second, 0 until a measurement is available.
    qint64 bytesPerSecond() const { return m_hasRate ? static_cast<qint64>(m_rate) : 0; }

    void reset();

private:
    qint64 m_lastTimeMs = -1;
    qint64 m_lastBytes = 0;
    double m_rate = 0.0;
    bool m_hasRate = false;
};

} // namespace launcher
