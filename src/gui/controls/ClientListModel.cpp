#include "pch.h"
/// @file ClientListModel.cpp
/// @brief Table model for client lists — implementation.

#include "controls/ClientListModel.h"

#include "client/ClientStateDefs.h"
#include "prefs/Preferences.h"
#include "utils/ClientIcons.h"
#include "utils/ClientStateText.h"
#include "utils/CountryFlags.h"
#include "utils/PriorityText.h"
#include "utils/StringUtils.h"

#include <QColor>

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

// Column counts per mode (matching MFC screenshots), each +1 for MorphXT's Country
// and +1 for Shared List (report #11; appended so saved layouts still restore)
constexpr int UploadingColCount    = 10; // +Upload Time
constexpr int DownloadingColCount  = 10; // two Transfer columns
constexpr int OnQueueColCount      = 12; // File Pri, Rating, Score, Asked, Last Seen, Entered Queue, Banned, Obtained Parts
constexpr int KnownClientsColCount = 10; // +Connected

} // anonymous namespace

ClientListModel::ClientListModel(ClientListMode mode, QObject* parent)
    : AbstractTableModel<ClientRow>(parent)
    , m_mode(mode)
{
}

std::vector<int> ClientListModel::extColumns() const
{
    std::vector<int> cols{sharedListColumn()};
    if (hasExtraColumns()) {
        for (int i = 0; i < ExtraCount; ++i)
            cols.push_back(baseColumnCount() + i);
    }
    return cols;
}

std::vector<int> ClientListModel::defaultHiddenColumns() const
{
    std::vector<int> cols = extColumns();
    cols.push_back(countryColumn());
    return cols;
}

int ClientListModel::columnCountValue() const
{
    return baseColumnCount() + (hasExtraColumns() ? ExtraCount : 0);
}

QVariant ClientListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_rows.size()))
        return {};

    const auto& c = m_rows[static_cast<size_t>(index.row())];
    if (index.column() >= baseColumnCount())
        return extraData(c, index.column() - baseColumnCount(), role);

    // MFC's LVCFMT_RIGHT columns: UploadListCtrl.cpp:77-78, DownloadClientsCtrl.cpp:66-69,
    // ClientListCtrl.cpp:66,68. The queue list has none.
    if (role == Qt::TextAlignmentRole) {
        const int col = index.column();
        const bool right = (m_mode == ClientListMode::Uploading && (col == 2 || col == 3))
                        || (m_mode == ClientListMode::Downloading && (col == 3 || col == 5 || col == 6))
                        || (m_mode == ClientListMode::KnownClients && (col == 2 || col == 4));
        return right ? static_cast<int>(Qt::AlignRight | Qt::AlignVCenter) : static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }

    const bool isCountry = index.column() == countryColumn();

    // ed3k upload queue rows: marked like the Messages panel's, the place instead of a score
    if (c.network == ClientNetwork::Ed3k && role == Qt::DisplayRole) {
        if (index.column() == 0)
            return tr("%1 (ed3k)").arg(clientNameText(c.userName));
        if (m_mode == ClientListMode::OnQueue && index.column() == 4)
            return c.remoteQueueRank > 0 ? tr("place %1").arg(c.remoteQueueRank) : QString();
    }

    if (index.column() == sharedListColumn()) {
        if (c.isUrl || c.network == ClientNetwork::Ed3k)
            return {};   // HTTP source / ed3k node, no eD2K shared list
        if (role == Qt::DisplayRole)
            return c.noViewShared ? tr("No") : tr("Yes");
        if (role == Qt::UserRole)
            return c.noViewShared ? 0 : 1;
    }

    if (role == Qt::DisplayRole)
        return isCountry ? QVariant(CountryFlags::columnText(c.cc)) : displayData(c, index.column());

    if (role == Qt::UserRole)
        return isCountry ? QVariant(CountryFlags::sortKey(c.cc)) : sortData(c, index.column());

    if (role == UpStatusRole)
        return QVariant::fromValue(c.upStatus);

    if (role == PartMapRole)
        return c.partMap;

    if (role == FileSizeRole)
        return QVariant::fromValue(c.reqFileSize);

    if (role == Qt::DecorationRole && index.column() == 0)
        return CountryFlags::withFlag(clientSoftwareIcon(c.softwareId, c.hasCredit, c.isFriend), c.cc);

    if (role == Qt::ToolTipRole && (index.column() == 0 || isCountry) && !c.cc.isEmpty())
        return CountryFlags::tooltip(c.cc);

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
    if (section == countryColumn())
        return tr("Country");
    if (section == sharedListColumn())
        return tr("Shared List");
    switch (section - baseColumnCount()) {
    case ExtraUploaded:     return tr("Uploaded");
    case ExtraDownloaded:   return tr("Downloaded");
    case ExtraRatio:        return tr("Ratio");
    case ExtraSessionRatio: return tr("Session Ratio");
    default:                return headerLabel(section);
    }
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

int ClientListModel::baseColumnCount() const
{
    switch (m_mode) {
    case ClientListMode::Uploading:    return UploadingColCount;
    case ClientListMode::Downloading:  return DownloadingColCount;
    case ClientListMode::OnQueue:      return OnQueueColCount;
    case ClientListMode::KnownClients: return KnownClientsColCount;
    }
    return 0;
}

bool ClientListModel::hasExtraColumns() const
{
    return m_mode == ClientListMode::Uploading || m_mode == ClientListMode::OnQueue;
}

QVariant ClientListModel::extraData(const ClientRow& c, int extra, int role) const
{
    if (role == Qt::TextAlignmentRole)
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);

    // Uploaded / Downloaded: the peer's credit totals, kept across restarts in clients.met.
    // Ratio columns: the upload file's bytes sent to anyone, per file size (eMuleAI).
    if (role == Qt::DisplayRole) {
        switch (extra) {
        case ExtraUploaded:     return sizeCell(c.uploadedTotal);
        case ExtraDownloaded:   return sizeCell(c.downloadedTotal);
        case ExtraRatio:        return formatShareRatio(c.uploadFileAllTimeTransferred, c.uploadFileSize);
        case ExtraSessionRatio: return formatShareRatio(c.uploadFileTransferred, c.uploadFileSize);
        default:                return {};
        }
    }
    if (role == Qt::UserRole) {
        switch (extra) {
        case ExtraUploaded:     return QVariant::fromValue(c.uploadedTotal);
        case ExtraDownloaded:   return QVariant::fromValue(c.downloadedTotal);
        case ExtraRatio:        return shareRatio(c.uploadFileAllTimeTransferred, c.uploadFileSize);
        case ExtraSessionRatio: return shareRatio(c.uploadFileTransferred, c.uploadFileSize);
        default:                return {};
        }
    }
    return {};
}

QVariant ClientListModel::displayData(const ClientRow& c, int column) const
{
    switch (m_mode) {
    case ClientListMode::Uploading:
        // MFC: User Name, File, Speed, Transferred, Waited, Upload Time, Status, Obtained Parts
        switch (column) {
        case 0: return clientNameText(c.userName);
        case 1: return c.fileName;
        case 2: return rateCell(c.upDatarate);
        // This slot's bytes, with the credit total in brackets as in the Downloading list.
        // Diverges on purpose (issue #51): MFC's UploadListCtrl.cpp:200-203 brackets the
        // slot's file payload instead, which reads as the same number twice.
        case 3: return sessionWithTotal(c.sessionUp, c.uploadedTotal);
        // MFC UploadListCtrl.cpp:205-213
        case 4: {
            const QString lowId = lowIdText(c);
            return lowId.isEmpty() ? hmCell(c.waitStartTime)
                                   : QStringLiteral("%1 (%2)").arg(hmCell(c.waitStartTime), lowId);
        }
        case 5: return hmCell(c.uploadStartDelay);
        case 6: return uploadStateText(c.uploadState, c.uploadStalled && thePrefs.showExtControls());
        case 7: return {};   // UploadStatusDelegate draws the bar
        default: return {};
        }

    case ClientListMode::Downloading:
        // MFC: User Name, Software, File, Speed, Available Parts, Transferred Down, Transferred Up, Source Type
        switch (column) {
        case 0: return clientNameText(c.userName);
        case 1: return c.software;
        case 2: return c.fileName;
        case 3: return rateCell(c.downDatarate);
        case 4: return {};   // SourcePartsDelegate draws the bar
        case 5: return sessionWithTotal(c.sessionDown, c.downloadedTotal);
        case 6: return sessionWithTotal(c.sessionUp, c.uploadedTotal);
        case 7: return sourceFromStr(c.sourceFrom);
        default: return {};
        }

    case ClientListMode::OnQueue:
        // MFC: User Name, File, File Priority, Rating, Score, Asked, Last Seen, Entered Queue, Banned, Obtained Parts
        // (QueueListCtrl.cpp:185-260)
        switch (column) {
        case 0: return clientNameText(c.userName);
        case 1: return c.fileName;
        case 2: return c.uploadFilePriority >= 0
                     ? uploadPriorityText(c.uploadFilePriority, c.uploadFileAutoPriority) : QString{};
        case 3: return QString::number(c.queueRating);
        case 4: {
            if (c.hasLowID && c.addNextConnect)
                return QStringLiteral("%1 ****").arg(c.queueScore);
            const QString lowId = lowIdText(c);
            return lowId.isEmpty() ? QString::number(c.queueScore)
                                   : QStringLiteral("%1 (%2)").arg(QString::number(c.queueScore), lowId);
        }
        case 5: return QString::number(c.askedCount);
        case 6: return hmCell(c.lastUpRequestDelay);
        case 7: return hmCell(c.waitStartTime);
        case 8: return c.isBanned ? tr("Yes") : tr("No");
        case 9: return {};   // UploadStatusDelegate draws the bar
        default: return {};
        }

    case ClientListMode::KnownClients:
        // MFC: User Name, Upload Status, Transferred Up, Download Status, Transferred Down, Software, Connected, Hash
        // (ClientListCtrl.cpp:166-200); the two byte columns are the credit totals.
        switch (column) {
        case 0: return clientNameText(c.userName);
        case 1: return uploadStateText(c.uploadState, c.uploadStalled && thePrefs.showExtControls());
        case 2: return sizeCell(c.uploadedTotal);
        case 3: return downloadStateText(c.downloadState, c.remoteQueueFull);
        case 4: return sizeCell(c.downloadedTotal);
        case 5: return c.software.isEmpty() ? tr("Unknown") : c.software;
        case 6: return c.isConnected ? tr("Yes") : tr("No");
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
        case 6: return uploadStateRank(c.uploadState);   // by state, as MFC
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
        case 1: return uploadStateRank(c.uploadState);
        case 2: return QVariant::fromValue(c.uploadedTotal);
        case 3: return c.downloadState;
        case 4: return QVariant::fromValue(c.downloadedTotal);
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
        case 2: return tr("Transferred Up");
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

QString ClientListModel::lowIdText(const ClientRow& c)
{
    QString route;
    if (c.natRoute == QLatin1String("quic"))
        route = tr("QUIC");
    else if (c.natRoute == QLatin1String("ipv6"))
        route = tr("IPv6");
    if (!c.hasLowID)
        return route;
    return route.isEmpty() ? tr("Low ID") : tr("Low ID, %1").arg(route);
}

} // namespace eMule
