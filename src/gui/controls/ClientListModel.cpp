#include "pch.h"
/// @file ClientListModel.cpp
/// @brief Table model for client lists — implementation.

#include "controls/ClientListModel.h"

#include "client/ClientStateDefs.h"
#include "prefs/Preferences.h"
#include "utils/PriorityText.h"
#include "utils/StringUtils.h"

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

namespace eMule {

namespace {

/// Client lists leave a zero byte count / idle rate blank rather than "0 Bytes".
QString sizeCell(int64_t bytes)
{
    return bytes > 0 ? formatByteSize(bytes) : QString{};
}

QString rateCell(int64_t bytesPerSec)
{
    return bytesPerSec > 0 ? formatByteRate(bytesPerSec) : QString{};
}

/// The session figure, with the credit total in brackets when it is larger
/// (MFC DownloadClientsCtrl.cpp:187-198).
QString sessionWithTotal(int64_t session, int64_t total)
{
    if (total <= session)
        return sizeCell(session);
    return QStringLiteral("%1 (%2)").arg(formatByteSize(session), formatByteSize(total));
}

/// Elapsed milliseconds as MFC CastSecondsToHM shows them.
QString hmCell(int64_t ms)
{
    return formatSecondsHM(ms / 1000);
}

/// SourceFrom enum to display string.
///
/// The daemon sends the raw core value — CborSerializers.h pushes
/// static_cast<int>(client->sourceFrom()) — so these cases must track SourceFrom
/// (ClientStateDefs.h) exactly. This table used to start at "None" and was shifted one
/// position off the enum, mislabelling every source in the panel.
QString sourceFromStr(int sf)
{
    switch (sf) {
    case 0:  return QObject::tr("Server");        // SourceFrom::Server
    case 1:  return QObject::tr("Kad");           // SourceFrom::Kademlia
    case 2:  return QObject::tr("Source Exch.");  // SourceFrom::SourceExchange
    case 3:  return QObject::tr("Passive");       // SourceFrom::Passive
    case 4:  return QObject::tr("Link");          // SourceFrom::Link
    case 7:  return QObject::tr("SLS");           // SourceFrom::SLS (saved source list)
    case 8:  return QObject::tr("HTTP Cache");    // SourceFrom::HttpCache
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
    : AbstractTableModel<ClientRow>(parent)
    , m_mode(mode)
{
}

int ClientListModel::columnCountValue() const
{
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
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size()))
        return {};

    const auto& c = m_rows[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole)
        return displayData(c, index.column());

    if (role == Qt::UserRole)
        return sortData(c, index.column());

    if (role == UpStatusRole)
        return QVariant::fromValue(c.upStatus);

    if (role == Qt::DecorationRole && index.column() == 0)
        return clientSoftwareIcon(c.softwareId, c.hasCredit, c.isFriend);

    // Same teal the downloads list uses for an HTTP Cache source, so the two
    // halves of the feature read the same way. MFC gave PeerCache its own bar
    // for exactly this reason.
    if (role == Qt::ForegroundRole
        && c.sourceFrom == static_cast<int>(SourceFrom::HttpCache))
        return QColor(0x00, 0x99, 0x99);

    return {};
}

QVariant ClientListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    return headerLabel(section);
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
        case 2: return rateCell(c.upDatarate);
        // Session, not lifetime: MFC's UploadListCtrl.cpp:200-203 shows GetSessionUp(), and
        // in advanced mode appends the file payload alone — the gap between the two is the
        // ed2k + in-packet framing this slot has paid for.
        case 3: return thePrefs.showExtControls()
                     ? QStringLiteral("%1 (%2)").arg(sizeCell(c.sessionUp),
                                                     sizeCell(c.queueSessionPayloadUp))
                     : sizeCell(c.sessionUp);
        // MFC UploadListCtrl.cpp:205-213
        case 4: return c.hasLowID ? QStringLiteral("%1 (%2)").arg(hmCell(c.waitStartTime), tr("Low ID"))
                                  : hmCell(c.waitStartTime);
        case 5: return hmCell(c.uploadStartDelay);
        case 6: return c.uploadState;
        case 7: return {};   // UploadStatusDelegate draws the bar
        default: return {};
        }

    case ClientListMode::Downloading:
        // MFC: User Name, Software, File, Speed, Available Parts, Transferred Down, Transferred Up, Source Type
        switch (column) {
        case 0: return c.userName;
        case 1: return c.software;
        case 2: return c.fileName;
        case 3: return rateCell(c.downDatarate);
        case 4: return c.availPartCount > 0 ? QString::number(c.availPartCount) : QString{};
        case 5: return sessionWithTotal(c.sessionDown, c.downloadedTotal);
        case 6: return sessionWithTotal(c.sessionUp, c.uploadedTotal);
        case 7: return sourceFromStr(c.sourceFrom);
        default: return {};
        }

    case ClientListMode::OnQueue:
        // MFC: User Name, File, File Priority, Rating, Score, Asked, Last Seen, Entered Queue, Banned, Obtained Parts
        // (QueueListCtrl.cpp:185-260)
        switch (column) {
        case 0: return c.userName;
        case 1: return c.fileName;
        case 2: return c.uploadFilePriority >= 0
                     ? uploadPriorityText(c.uploadFilePriority, c.uploadFileAutoPriority) : QString{};
        case 3: return QString::number(c.queueRating);
        case 4:
            if (!c.hasLowID)
                return QString::number(c.queueScore);
            return c.addNextConnect
                ? QStringLiteral("%1 ****").arg(c.queueScore)
                : QStringLiteral("%1 (%2)").arg(QString::number(c.queueScore), tr("Low ID"));
        case 5: return QString::number(c.askedCount);
        case 6: return hmCell(c.lastUpRequestDelay);
        case 7: return hmCell(c.waitStartTime);
        case 8: return c.isBanned ? tr("Yes") : tr("No");
        case 9: return {};   // UploadStatusDelegate draws the bar
        default: return {};
        }

    case ClientListMode::KnownClients:
        // MFC: User Name, Upload Status, Transferred, Download Status, Transferred Down, Software, Connected, Hash
        switch (column) {
        case 0: return c.userName;
        case 1: return c.uploadState;
        case 2: return sizeCell(c.transferredUp);
        case 3: return c.downloadState;
        case 4: return sizeCell(c.transferredDown);
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
        case 2: return QVariant::fromValue(c.upDatarate);
        case 3: return QVariant::fromValue(c.sessionUp);
        case 4: return QVariant::fromValue(c.waitStartTime);
        case 5: return QVariant::fromValue(c.uploadStartDelay);
        case 6: return c.uploadState;
        case 7: return c.upPartCount;
        default: return {};
        }

    case ClientListMode::Downloading:
        switch (column) {
        case 0: return c.userName;
        case 1: return c.software;
        case 2: return c.fileName;
        case 3: return QVariant::fromValue(c.downDatarate);
        case 4: return c.availPartCount;
        case 5: return QVariant::fromValue(c.sessionDown);
        case 6: return QVariant::fromValue(c.sessionUp);
        case 7: return c.sourceFrom;
        default: return {};
        }

    case ClientListMode::OnQueue:
        switch (column) {
        case 0: return c.userName;
        case 1: return c.fileName;
        // MFC SortProc (QueueListCtrl.cpp:340-367): Very Low (4) ranks below Low; the two
        // times sort by timestamp, i.e. the longest ago first.
        case 2: return c.uploadFilePriority < 0 ? -2 : (c.uploadFilePriority == 4 ? -1 : c.uploadFilePriority);
        case 3: return QVariant::fromValue(c.queueRating);
        case 4: return QVariant::fromValue(c.queueScore);
        case 5: return QVariant::fromValue(c.askedCount);
        case 6: return QVariant::fromValue(-c.lastUpRequestDelay);
        case 7: return QVariant::fromValue(-c.waitStartTime);
        case 8: return c.isBanned ? 1 : 0;
        case 9: return c.upPartCount;
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
        case 5: return tr("Transferred Down");
        case 6: return tr("Transferred Up");
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
