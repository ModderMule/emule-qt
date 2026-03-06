#include "pch.h"
/// @file ClientListModel.cpp
/// @brief Table model for client lists — implementation.

#include "controls/ClientListModel.h"

#include <QDateTime>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

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

/// Format a duration in milliseconds as HH:MM:SS.
QString formatDuration(int64_t ms)
{
    if (ms <= 0)
        return {};
    const int64_t totalSecs = ms / 1000;
    const int h = static_cast<int>(totalSecs / 3600);
    const int m = static_cast<int>((totalSecs % 3600) / 60);
    const int s = static_cast<int>(totalSecs % 60);
    return QStringLiteral("%1:%2:%3")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

/// Priority display string matching MFC eMule (same as SharedFilesModel).
QString priorityStr(int prio, bool isAuto)
{
    QString name;
    switch (prio) {
    case 4:  name = QObject::tr("Very Low");  break;
    case 0:  name = QObject::tr("Low");       break;
    case 1:  name = QObject::tr("Normal");    break;
    case 2:  name = QObject::tr("High");      break;
    case 3:  name = QObject::tr("Very High"); break;
    default: name = QObject::tr("Normal");    break;
    }
    if (isAuto)
        return QObject::tr("Auto [%1]").arg(name);
    return name;
}

/// File rating display string matching MFC GetRateString().
QString ratingStr(uint8_t r)
{
    switch (r) {
    case 1: return QObject::tr("Fake");
    case 2: return QObject::tr("Poor");
    case 3: return QObject::tr("Fair");
    case 4: return QObject::tr("Good");
    case 5: return QObject::tr("Excellent");
    default: return {};
    }
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

// ClientSoftware enum values from ClientStateDefs.h
constexpr int SoftEMule          = 0;
constexpr int SoftCDonkey        = 1;
constexpr int SoftXMule          = 2;
constexpr int SoftAMule          = 3;
constexpr int SoftShareaza       = 4;
constexpr int SoftMLDonkey       = 10;
constexpr int SoftLphant         = 20;
constexpr int SoftEDonkeyHybrid  = 50;
// SoftEDonkey (51) and SoftOldEMule (52) fall through to default icon
constexpr int SoftURL            = 53;

/// Get client software icon matching MFC GetDisplayImage() logic.
/// "Plus" variants indicate the client has credit (scoreRatio > 1.0).
/// Friend clients show the software icon with a small friend badge overlay
/// at the bottom-right corner (preserving software identity).
QIcon clientSoftwareIcon(int softwareId, bool hasCredit, bool isFriend)
{
    static QHash<int, QIcon> cache;

    // URL source → Server icon (MFC index 15)
    if (softwareId == SoftURL) {
        static QIcon urlIcon(QStringLiteral(":/icons/Server.ico"));
        return urlIcon;
    }

    // Cache key: combine softwareId + hasCredit + isFriend
    const int key = softwareId * 4 + (hasCredit ? 2 : 0) + (isFriend ? 1 : 0);
    auto it = cache.find(key);
    if (it != cache.end())
        return it.value();

    QString path;
    switch (softwareId) {
    case SoftEMule:
    case SoftCDonkey:
    case SoftXMule:
        path = hasCredit ? QStringLiteral(":/icons/ClientCompatiblePlus.ico")
                         : QStringLiteral(":/icons/ClientCompatible.ico");
        break;
    case SoftAMule:
        path = hasCredit ? QStringLiteral(":/icons/ClientaMulePlus.ico")
                         : QStringLiteral(":/icons/ClientaMule.ico");
        break;
    case SoftShareaza:
        path = hasCredit ? QStringLiteral(":/icons/ClientShareazaPlus.ico")
                         : QStringLiteral(":/icons/ClientShareaza.ico");
        break;
    case SoftMLDonkey:
        path = hasCredit ? QStringLiteral(":/icons/ClientMLDonkeyPlus.ico")
                         : QStringLiteral(":/icons/ClientMLDonkey.ico");
        break;
    case SoftLphant:
        path = hasCredit ? QStringLiteral(":/icons/ClientlPhantPlus.ico")
                         : QStringLiteral(":/icons/ClientlPhant.ico");
        break;
    case SoftEDonkeyHybrid:
        path = hasCredit ? QStringLiteral(":/icons/ClienteDonkeyHybridPlus.ico")
                         : QStringLiteral(":/icons/ClienteDonkeyHybrid.ico");
        break;
    default:
        path = hasCredit ? QStringLiteral(":/icons/ClientDefaultPlus.ico")
                         : QStringLiteral(":/icons/ClientDefault.ico");
        break;
    }

    if (!isFriend) {
        QIcon icon(path);
        cache.insert(key, icon);
        return icon;
    }

    // Composite: software icon + friend badge at bottom-right
    QPixmap pixmap = QIcon(path).pixmap(16, 16);
    {
        QPainter painter(&pixmap);
        static const QPixmap friendBadge =
            QIcon(QStringLiteral(":/icons/Friend.ico")).pixmap(10, 10);
        painter.drawPixmap(6, 6, 10, 10, friendBadge);
    }
    QIcon composite(pixmap);
    cache.insert(key, composite);
    return composite;
}

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

    if (role == Qt::DecorationRole && index.column() == 0)
        return clientSoftwareIcon(c.softwareId, c.hasCredit, c.isFriend);

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

const ClientRow* ClientListModel::clientAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_clients.size()))
        return nullptr;
    return &m_clients[static_cast<size_t>(row)];
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
        case 5: return c.uploadStartDelay > 0 ? formatDuration(c.uploadStartDelay) : QString{};
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
        case 2: return c.filePriority >= 0 ? priorityStr(c.filePriority, c.isAutoPriority) : QString{};
        case 3: return c.fileRating > 0 ? ratingStr(c.fileRating) : QString{};
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
        case 6: return c.isConnected ? QObject::tr("Yes") : QString{};
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
        case 5: return QVariant::fromValue(c.uploadStartDelay);
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
        case 2: return c.filePriority;
        case 3: return static_cast<int>(c.fileRating);
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
        case 6: return c.isConnected ? 1 : 0;
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
