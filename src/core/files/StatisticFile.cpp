/// @file StatisticFile.cpp
/// @brief Per-file transfer statistics — port of MFC CStatisticFile.

#include "files/StatisticFile.h"

namespace eMule {

void StatisticFile::mergeFileStats(const StatisticFile& other)
{
    m_requested += other.m_requested;
    m_accepted += other.m_accepted;
    m_transferred += other.m_transferred;
    m_allTimeRequested += other.m_allTimeRequested;
    m_allTimeAccepted += other.m_allTimeAccepted;
    m_allTimeTransferred += other.m_allTimeTransferred;
}

void StatisticFile::addRequest()
{
    ++m_requested;
    ++m_allTimeRequested;
}

void StatisticFile::addAccepted()
{
    ++m_accepted;
    ++m_allTimeAccepted;
}

void StatisticFile::addTransferred(uint64 bytes)
{
    m_transferred += bytes;
    m_allTimeTransferred += bytes;
}

} // namespace eMule
