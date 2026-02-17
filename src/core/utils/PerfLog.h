#pragma once

/// @file PerfLog.h
/// @brief Performance logging — replaces MFC CPerfLog.
///
/// Periodically samples transfer rates and writes them to a log file
/// in CSV or MRTG format.

#include "Types.h"

#include <QString>

#include <cstdint>

namespace eMule {

/// Performance sample logger (replaces MFC CPerfLog).
class PerfLog {
public:
    enum class Mode : uint8 {
        None       = 0,
        OneSample  = 1,
        AllSamples = 2
    };

    enum class Format : uint8 {
        CSV  = 0,
        MRTG = 1
    };

    PerfLog();

    void startup(const QString& filePath, Mode mode, Format format, uint32 intervalMs);
    void shutdown();
    void logSamples(uint32 curDownRate, uint32 curUpRate,
                    uint32 curDownOH, uint32 curUpOH);

private:
    void writeSamples(uint32 curDn, uint32 curUp, uint32 curDnOH, uint32 curUpOH);

    uint32  m_interval = 0;
    uint64  m_lastSampled = 0;
    QString m_filePath;
    uint64  m_lastSessionSentBytes = 0;
    uint64  m_lastSessionRecvBytes = 0;
    uint64  m_lastDnOH = 0;
    uint64  m_lastUpOH = 0;
    Mode    m_mode = Mode::None;
    Format  m_format = Format::CSV;
    bool    m_initialized = false;
};

} // namespace eMule
