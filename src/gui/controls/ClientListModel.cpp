/// @file ClientListModel.cpp
/// @brief Table model for client lists — implementation.

#include "controls/ClientListModel.h"

#include <QDateTime>

namespace eMule {

namespace {

/// Format a byte count for display.
QString formatSize(int64_t bytes)
{
    if (bytes <= 0)
        return {};
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GiB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

/// Format a speed value.
QString formatSpeed(int64_t bytesPerSec)
{
    if (bytesPerSec <= 0)
        return {};
    if (bytesPerSec < 1024)
        return QStringLiteral("%1 B/s").arg(bytesPerSec);
    return QStringLiteral("%1 KiB/s").arg(bytesPerSec / 1024.0, 0, 'f', 1);
}

/// Format wait time as duration.
QString formatWaitTime(int64_t startTime)
{
    if (startTime <= 0)
        return {};
    const int64_t now = QDateTime::currentSecsSinceEpoch();
    const int64_t secs = now - startTime;
    if (secs < 60)
        return QStringLiteral("%1s").arg(secs);
    if (secs < 3600)
        return QStringLiteral("%1m %2s").arg(secs / 60).arg(secs % 60);
    return QStringLiteral("%1h %2m").arg(secs / 3600).arg((secs % 3600) / 60);
}

/// SourceFrom enum to display string.
QString sourceFromStr(int sf)
{
    switch (sf) {
    case 0:  return QObject::tr("None");
    case 1:  return QObject::tr("Server");
    case 2:  return QObject::tr("Kad");
    case 3:  return QObject::tr("Source Exch.");
    case 4:  return QObject::tr("Passive");
    case 5:  return QObject::tr("Link");
    default: return QStringLiteral("?");
    }
}

// Column counts per mode (matching MFC screenshots)
constexpr int UploadingColCount    = 8;  // +Upload Time
constexpr int DownloadingColCount  = 8;  // two Transfer columns
constexpr int OnQueueColCount      = 10; // File Pri, Rating, Score, Asked, Last Seen, Entered Queue, Banned, Obtained Parts
constexpr int KnownClientsColCount = 8;  // +Connected

} // anonymous namespace

ClientListModel::ClientListModel(ClientListMode mode, QObject* parent)
    : QAbstractTableModel(parent)
    , m_mode(mode)
{
}

int ClientListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_clients.size());
}

int ClientListModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    switch (m_mode) {
    case ClientListMode::Uploading:    return UploadingColCount;
    case ClientListMode::Downloading:  return DownloadingColCount;
    case ClientListMode::OnQueue:      return OnQueueColCount;
    case ClientListMode::KnownClients: return KnownClientsColCount;
    }
    return 0;
}

QVariant ClientListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_clients.size()))
        return {};

    const auto& c = m_clients[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole)
        return displayData(c, index.column());

    if (role == Qt::UserRole)
        return sortData(c, index.column());

    return {};
}

QVariant ClientListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    return headerLabel(section);
}

void ClientListModel::setClients(std::vector<ClientRow> clients)
{
    beginResetModel();
    m_clients = std::move(clients);
    endResetModel();
}

void ClientListModel::clear()
{
    beginResetModel();
    m_clients.clear();
    endResetModel();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

QVariant ClientListModel::displayData(const ClientRow& c, int column) const
{
    switch (m_mode) {
    case ClientListMode::Uploading:
        // MFC: User Name, File, Speed, Transferred, Waited, Upload Time, Status, Obtained Parts
        switch (column) {
        case 0: return c.userName;
        case 1: return c.fileName;
        case 2: return formatSpeed(c.sessionUp > 0 ? c.sessionUp : 0);
        case 3: return formatSize(c.transferredUp);
        case 4: return formatWaitTime(c.waitStartTime);
        case 5: return {}; // Upload Time (ToDo: need upload start time from daemon)
        case 6: return c.uploadState;
        case 7: return c.partCount > 0 ? QString::number(c.partCount) : QString{};
        default: return {};
        }

    case ClientListMode::Downloading:
        // MFC: User Name, Software, File, Speed, Available Parts, Transferred, Transferred, Source Type
        switch (column) {
        case 0: return c.userName;
        case 1: return c.software;
        case 2: return c.fileName;
        case 3: return formatSpeed(c.sessionDown > 0 ? c.sessionDown : 0);
        case 4: return c.availPartCount > 0 ? QString::number(c.availPartCount) : QString{};
        case 5: return formatSize(c.sessionDown);
        case 6: return formatSize(c.transferredDown);
        case 7: return sourceFromStr(c.sourceFrom);
        default: return {};
        }

    case ClientListMode::OnQueue:
        // MFC: User Name, File, File Priority, Rating, Score, Asked, Last Seen, Entered Queue, Banned, Obtained Parts
        switch (column) {
        case 0: return c.userName;
        case 1: return c.fileName;
        case 2: return {}; // File Priority (ToDo)
        case 3: return {}; // Rating (ToDo)
        case 4: return c.remoteQueueRank > 0 ? QString::number(c.remoteQueueRank) : QString{};
        case 5: return c.askedCount > 0 ? QString::number(c.askedCount) : QString{};
        case 6: return formatWaitTime(c.waitStartTime);
        case 7: return formatWaitTime(c.waitStartTime); // Entered Queue
        case 8: return c.isBanned ? QObject::tr("Yes") : QString{};
        case 9: return c.partCount > 0 ? QString::number(c.partCount) : QString{};
        default: return {};
        }

    case ClientListMode::KnownClients:
        // MFC: User Name, Upload Status, Transferred, Download Status, Transferred Down, Software, Connected, Hash
        switch (column) {
        case 0: return c.userName;
        case 1: return c.uploadState;
        case 2: return formatSize(c.transferredUp);
        case 3: return c.downloadState;
        case 4: return formatSize(c.transferredDown);
        case 5: return c.software;
        case 6: return {}; // Connected (ToDo)
        case 7: return c.userHash;
        default: return {};
        }
    }

    return {};
}

QVariant ClientListModel::sortData(const ClientRow& c, int column) const
{
    switch (m_mode) {
    case ClientListMode::Uploading:
        switch (column) {
        case 0: return c.userName;
        case 1: return c.fileName;
        case 2: return QVariant::fromValue(c.sessionUp);
        case 3: return QVariant::fromValue(c.transferredUp);
        case 4: return QVariant::fromValue(c.waitStartTime);
        case 5: return QVariant::fromValue(int64_t{0});
        case 6: return c.uploadState;
        case 7: return c.partCount;
        default: return {};
        }

    case ClientListMode::Downloading:
        switch (column) {
        case 0: return c.userName;
        case 1: return c.software;
        case 2: return c.fileName;
        case 3: return QVariant::fromValue(c.sessionDown);
        case 4: return c.availPartCount;
        case 5: return QVariant::fromValue(c.sessionDown);
        case 6: return QVariant::fromValue(c.transferredDown);
        case 7: return c.sourceFrom;
        default: return {};
        }

    case ClientListMode::OnQueue:
        switch (column) {
        case 0: return c.userName;
        case 1: return c.fileName;
        case 2: return {};
        case 3: return {};
        case 4: return c.remoteQueueRank;
        case 5: return QVariant::fromValue(c.askedCount);
        case 6: return QVariant::fromValue(c.waitStartTime);
        case 7: return QVariant::fromValue(c.waitStartTime);
        case 8: return c.isBanned ? 1 : 0;
        case 9: return c.partCount;
        default: return {};
        }

    case ClientListMode::KnownClients:
        switch (column) {
        case 0: return c.userName;
        case 1: return c.uploadState;
        case 2: return QVariant::fromValue(c.transferredUp);
        case 3: return c.downloadState;
        case 4: return QVariant::fromValue(c.transferredDown);
        case 5: return c.software;
        case 6: return {};
        case 7: return c.userHash;
        default: return {};
        }
    }

    return {};
}

QVariant ClientListModel::headerLabel(int column) const
{
    switch (m_mode) {
    case ClientListMode::Uploading:
        switch (column) {
        case 0: return tr("User Name");
        case 1: return tr("File");
        case 2: return tr("Speed");
        case 3: return tr("Transferred");
        case 4: return tr("Waited");
        case 5: return tr("Upload Time");
        case 6: return tr("Status");
        case 7: return tr("Obtained Parts");
        default: return {};
        }

    case ClientListMode::Downloading:
        switch (column) {
        case 0: return tr("User Name");
        case 1: return tr("Software");
        case 2: return tr("File");
        case 3: return tr("Speed");
        case 4: return tr("Available Parts");
        case 5: return tr("Transferred");
        case 6: return tr("Transferred");
        case 7: return tr("Source Type");
        default: return {};
        }

    case ClientListMode::OnQueue:
        switch (column) {
        case 0: return tr("User Name");
        case 1: return tr("File");
        case 2: return tr("File Priority");
        case 3: return tr("Rating");
        case 4: return tr("Score");
        case 5: return tr("Asked");
        case 6: return tr("Last Seen");
        case 7: return tr("Entered Queue");
        case 8: return tr("Banned");
        case 9: return tr("Obtained Parts");
        default: return {};
        }

    case ClientListMode::KnownClients:
        switch (column) {
        case 0: return tr("User Name");
        case 1: return tr("Upload Status");
        case 2: return tr("Transferred");
        case 3: return tr("Download Status");
        case 4: return tr("Transferred Down");
        case 5: return tr("Software");
        case 6: return tr("Connected");
        case 7: return tr("Hash");
        default: return {};
        }
    }

    return {};
}

} // namespace eMule
