#include "downloads/TransferRate.h"

namespace launcher {

void TransferRate::addSample(qint64 timeMs, qint64 bytes)
{
    // The first sample, or a transfer that started over: measure from here.
    if (m_lastTimeMs < 0 || bytes < m_lastBytes || timeMs < m_lastTimeMs) {
        reset();
        m_lastTimeMs = timeMs;
        m_lastBytes = bytes;
        return;
    }

    const qint64 elapsed = timeMs - m_lastTimeMs;
    if (elapsed < kMinIntervalMs)
        return;

    const double measured = static_cast<double>(bytes - m_lastBytes) * 1000.0 / static_cast<double>(elapsed);
    m_rate = m_hasRate ? (1.0 - kWeight) * m_rate + kWeight * measured : measured;
    m_hasRate = true;
    m_lastTimeMs = timeMs;
    m_lastBytes = bytes;
}

void TransferRate::reset()
{
    m_lastTimeMs = -1;
    m_lastBytes = 0;
    m_rate = 0.0;
    m_hasRate = false;
}

} // namespace launcher
