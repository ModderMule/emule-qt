#include "pch.h"
/// @file PerfLog.cpp
/// @brief Performance logging implementation.

#include "PerfLog.h"
#include "TimeUtils.h"

#include <QFile>
#include <QTextStream>

namespace eMule {

PerfLog::PerfLog() = default;

void PerfLog::startup(const QString& filePath, Mode mode, Format format, uint32 intervalMs)
{
    m_filePath = filePath;
    m_mode = mode;
    m_format = format;
    m_interval = intervalMs;
    m_lastSampled = getTickCount();
    m_lastSessionSentBytes = 0;
    m_lastSessionRecvBytes = 0;
    m_lastDnOH = 0;
    m_lastUpOH = 0;
    m_initialized = true;
}

void PerfLog::shutdown()
{
    m_initialized = false;
}

void PerfLog::logSamples(uint32 curDownRate, uint32 curUpRate,
                         uint32 curDownOH, uint32 curUpOH)
{
    if (!m_initialized || m_mode == Mode::None)
        return;

    const auto now = getTickCount();
    if (now - m_lastSampled < m_interval)
        return;

    m_lastSampled = now;
    writeSamples(curDownRate, curUpRate, curDownOH, curUpOH);
}

void PerfLog::writeSamples(uint32 curDn, uint32 curUp, uint32 curDnOH, uint32 curUpOH)
{
    if (m_filePath.isEmpty())
        return;

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream out(&file);
    if (m_format == Format::CSV) {
        out << curDn << ',' << curUp << ',' << curDnOH << ',' << curUpOH << '\n';
    } else {
        // MRTG format: two values per line
        out << curDn << '\n' << curUp << '\n';
    }
}

} // namespace eMule
