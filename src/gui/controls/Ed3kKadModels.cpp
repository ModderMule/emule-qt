#include "pch.h"
#include "controls/Ed3kKadModels.h"

#include "utils/AddressSortKey.h"
#include "utils/CountryFlags.h"
#include "app/SkinIcons.h"

#include <QDateTime>
#include <QPainter>
#include <QPixmap>
#include <QStringList>

#include <algorithm>

namespace eMule {

namespace {

QString value(const QCborMap& m, const char* key)
{
    return m.value(QLatin1String(key)).toVariant().toString();
}

qint64 number(const QCborMap& m, const char* key)
{
    return m.value(QLatin1String(key)).toInteger();
}

/// "12 s", "3 min", "2 h" of milliseconds.
QString ageText(qint64 ms)
{
    const qint64 s = ms / 1000;
    if (s < 120)
        return QCoreApplication::translate("Ed3kKadModels", "%1 s").arg(s);
    if (s < 7200)
        return QCoreApplication::translate("Ed3kKadModels", "%1 min").arg(s / 60);
    return QCoreApplication::translate("Ed3kKadModels", "%1 h").arg(s / 3600);
}

/// Badge of the family dialled first: "4" blue, "6" green; a dual node gets a dot.
QIcon familyIcon(int family, bool dual)
{
    static std::array<QIcon, 4> cache;
    const std::size_t i = (family == 6 ? 2 : 0) + (dual ? 1 : 0);
    if (!cache[i].isNull())
        return cache[i];
    QIcon icon;
    for (const int px : {16, 32}) {
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal u = px / 16.0;
        p.setPen(Qt::NoPen);
        p.setBrush(family == 6 ? QColor(0x2e, 0x9e, 0x4f) : QColor(0x2f, 0x6f, 0xd0));
        p.drawRoundedRect(QRectF(1 * u, 2 * u, 14 * u, 12 * u), 3 * u, 3 * u);
        QFont f;
        f.setBold(true);
        f.setPixelSize(int(11 * u));
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(1 * u, 2 * u, dual ? 11 * u : 14 * u, 12 * u), Qt::AlignCenter,
                   family == 6 ? QStringLiteral("6") : QStringLiteral("4"));
        if (dual) {
            p.setBrush(QColor(0xff, 0xc4, 0x2e));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QRectF(11 * u, 3 * u, 3.5 * u, 3.5 * u));
        }
        p.end();
        icon.addPixmap(pm);
    }
    cache[i] = icon;
    return icon;
}

} // namespace

// ---------------------------------------------------------------------------
// Ed3kKadTableModel
// ---------------------------------------------------------------------------

Ed3kKadTableModel::Ed3kKadTableModel(QObject* parent)
    : AbstractTableModel(parent)
{
    connect(&m_resolver, &HostnameResolver::resolved, this, [this](const QString& a) { hostnameChanged(a); });
}

QVariant Ed3kKadTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= count() || index.column() >= columnCount())
        return {};
    const QCborMap& r = m_rows[static_cast<size_t>(index.row())];
    const Column& c = m_columns[static_cast<size_t>(index.column())];
    if (role == Qt::DisplayRole)
        return c.text(r);
    if (role == Qt::UserRole)
        return c.sort ? c.sort(r) : QVariant(c.text(r));
    return {};
}

QVariant Ed3kKadTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section >= columnCount())
        return {};
    if (section == m_hostColumn && m_resolver.enabled() && !m_hostHeader.isEmpty())
        return m_hostHeader;
    return m_columns[static_cast<size_t>(section)].title;
}

void Ed3kKadTableModel::setRows(const QCborArray& rows)
{
    std::vector<QCborMap> maps;
    maps.reserve(static_cast<size_t>(rows.size()));
    for (const auto& v : rows)
        maps.push_back(v.toMap());
    // polled: a reset would swallow a double click and drop the selection
    syncRows(std::move(maps), [](const QCborMap& m) { return value(m, "id"); });
}

void Ed3kKadTableModel::setResolveHostnames(bool enabled)
{
    if (m_hostColumn < 0 || m_resolver.enabled() == enabled)
        return;
    m_resolver.setEnabled(enabled);
    refreshHostnames();
    emit headerDataChanged(Qt::Horizontal, m_hostColumn, m_hostColumn);
    if (count() > 0)
        emit dataChanged(index(0, m_hostColumn), index(count() - 1, m_hostColumn),
                         {Qt::DisplayRole, Qt::ToolTipRole, Qt::UserRole});
}

void Ed3kKadTableModel::setHostnameColumn(int column, std::function<QString(const QCborMap&)> addressOf,
                                          const QString& header)
{
    m_hostColumn = column;
    m_addressOf = std::move(addressOf);
    m_hostHeader = header;
}

void Ed3kKadTableModel::refreshHostnames()
{
    if (m_hostColumn < 0)
        return;
    QStringList addresses;
    addresses.reserve(count());
    for (const auto& m : m_rows)
        addresses.append(hostAddress(m));
    m_resolver.sync(addresses);
}

void Ed3kKadTableModel::hostnameChanged(const QString& address)
{
    for (int r = 0; r < count(); ++r) {
        if (hostAddress(row(r)) == address)
            emit dataChanged(index(r, m_hostColumn), index(r, m_hostColumn),
                             {Qt::DisplayRole, Qt::ToolTipRole, Qt::UserRole});
    }
}

// ---------------------------------------------------------------------------
// Ed3kKadContactsModel
// ---------------------------------------------------------------------------

Ed3kKadContactsModel::Ed3kKadContactsModel(QObject* parent)
    : Ed3kKadTableModel(parent)
{
    // as eD2K Kad (KadContactsModel): Contact0-4, SrcUnknown is res\Client4.ico
    for (int i = 0; i < 5; ++i)
        m_icons[static_cast<size_t>(i)] = skinIcon(QStringLiteral(":/icons/Contact%1.ico").arg(i));
    m_icons[5] = skinIcon(QStringLiteral(":/icons/Client4.ico"));
    m_columns = {
        // "fails(version)" as eD2K Kad's "type(version)"
        {tr("Type"),
         [](const QCborMap& m) {
             const QString agent = value(m, "agent");
             return agent.isEmpty() ? QString::number(number(m, "fails"))
                                    : QStringLiteral("%1(%2)").arg(number(m, "fails")).arg(versionOf(agent));
         },
         [](const QCborMap& m) {
             return QVariant(QStringLiteral("%1 %2").arg(number(m, "fails"), 10, 10, QLatin1Char('0')).arg(versionOf(value(m, "agent"))));
         }},
        {tr("Node ID"), [](const QCborMap& m) { return value(m, "id"); }, {}},
        {tr("Country"), [](const QCborMap& m) { return CountryFlags::columnText(value(m, "cc")); }, {}},
        // family dialled first; "dual" when the node is in both tables
        {tr("Family"),
         [](const QCborMap& m) {
             const QString f = QStringLiteral("IPv%1").arg(number(m, "family"));
             return m.value(QStringLiteral("dual")).toBool() ? tr("%1 dual").arg(f) : f;
         },
         [](const QCborMap& m) { return QVariant(number(m, "family") * 2 + (m.value(QStringLiteral("dual")).toBool() ? 1 : 0)); }},
        {tr("Distance"), [](const QCborMap& m) { return value(m, "distance"); }, {}},
        {tr("Fails"), [](const QCborMap& m) { return QString::number(number(m, "fails")); },
         [](const QCborMap& m) { return QVariant(number(m, "fails")); }},
        {tr("Next check"),
         [](const QCborMap& m) {
             const qint64 s = number(m, "checkInS");
             return s > 0 ? ageText(s * 1000) : tr("due");
         },
         [](const QCborMap& m) { return QVariant(number(m, "checkInS")); }},
        // appended to keep saved layouts; address of the family dialled first
        {tr("IP"),
         [this](const QCborMap& m) {
             const QString ip = value(m, "ip");
             return ip.isEmpty() ? QString() : m_resolver.endpointText(ip, static_cast<uint16_t>(number(m, "port")));
         },
         [this](const QCborMap& m) {
             const QString ip = value(m, "ip");
             return QVariant(m_resolver.displayText(ip, addressSortKey(ip, static_cast<uint16_t>(number(m, "port")))));
         }},
    };
    setHostnameColumn(ColIp, [](const QCborMap& m) { return value(m, "ip"); }, tr("Hostname / IP"));
}

void Ed3kKadContactsModel::setContacts(const QCborArray& rows)
{
    setRows(rows);
    refreshHostnames();
}

QVariant Ed3kKadContactsModel::data(const QModelIndex& index, int role) const
{
    // if (role == Qt::DecorationRole && index.isValid() && index.column() == ColId && index.row() < count())
    //     return CountryFlags::withFlag({}, value(row(index.row()), "cc"));
    // status dot + flag on Type, as eD2K Kad; the flag delegate widens logical column 0
    if (role == Qt::DecorationRole && index.isValid() && index.column() == ColType && index.row() < count()) {
        const QCborMap& m = row(index.row());
        return CountryFlags::withFlag(m_icons[static_cast<size_t>(contactImage(m))], value(m, "cc"));
    }
    // the family dialled first at a glance (IPv4 blue, IPv6 green, dot: in both tables)
    if (role == Qt::DecorationRole && index.isValid() && index.column() == ColFamily && index.row() < count()) {
        const QCborMap& m = row(index.row());
        return familyIcon(int(number(m, "family")), m.value(QStringLiteral("dual")).toBool());
    }
    if (role == Qt::ToolTipRole && index.isValid() && index.row() < count()) {
        const QCborMap& m = row(index.row());
        if (index.column() == ColCountry)
            return CountryFlags::tooltip(value(m, "cc"));
        if (index.column() == ColIp && !value(m, "ip").isEmpty())
            return m_resolver.toolTip(value(m, "ip"), static_cast<uint16_t>(number(m, "port")));
        if (index.column() == ColType) {
            const QString agent = value(m, "agent");
            const QString tip = agent.isEmpty() ? tr("Not connected since restart") : agent;
            const QString cc = CountryFlags::tooltip(value(m, "cc"));
            return cc.isEmpty() ? tip : tip + QLatin1Char('\n') + cc;
        }
    }
    return Ed3kKadTableModel::data(index, role);
}

QString Ed3kKadContactsModel::versionOf(const QString& agent)
{
    // last word that starts with a digit: "eMuleQt 0.7.25", "packmule-node 0.1.0"
    const QStringList words = agent.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (auto it = words.crbegin(); it != words.crend(); ++it)
        if (it->front().isDigit())
            return *it;
    return agent;
}

int Ed3kKadContactsModel::contactImage(const QCborMap& m) const
{
    if (value(m, "agent").isEmpty())
        return 5;
    return static_cast<int>(std::min<qint64>(number(m, "fails"), 4));
}

// ---------------------------------------------------------------------------
// Ed3kKadLookupsModel
// ---------------------------------------------------------------------------

Ed3kKadLookupsModel::Ed3kKadLookupsModel(QObject* parent)
    : Ed3kKadTableModel(parent)
{
    m_columns = {
        {tr("Number"), [](const QCborMap& m) { return QString::number(number(m, "id")); },
         [](const QCborMap& m) { return QVariant(number(m, "id")); }},
        {tr("Purpose"), [](const QCborMap& m) { return value(m, "purpose"); }, {}},
        {tr("Name"), [](const QCborMap& m) { return value(m, "name"); }, {}},
        {tr("Target"), [](const QCborMap& m) { return value(m, "target"); }, {}},
        {tr("Kind"),
         [](const QCborMap& m) {
             const QString kind = kindText(int(number(m, "kind")));
             return m.value(QStringLiteral("store")).toBool() ? tr("Store %1").arg(kind) : kind;
         },
         {}},
        // MFC Kad's Load: average of the responsible answers, empty without one
        {tr("Load"),
         [](const QCborMap& m) {
             const auto load = number(m, "load");
             return load < 0 ? QString() : QStringLiteral("%1%").arg(load);
         },
         [](const QCborMap& m) { return QVariant(number(m, "load")); }},
        // MFC Kad's Packets Sent: KadRequests by datagram or over QUIC, pages and retries too
        {tr("Requests Sent"), [](const QCborMap& m) { return QString::number(number(m, "sent")); },
         [](const QCborMap& m) { return QVariant(number(m, "sent")); }},
        {tr("Asked"), [](const QCborMap& m) { return QString::number(number(m, "asked")); },
         [](const QCborMap& m) { return QVariant(number(m, "asked")); }},
        {tr("Answered"), [](const QCborMap& m) { return QString::number(number(m, "answered")); },
         [](const QCborMap& m) { return QVariant(number(m, "answered")); }},
        {tr("Candidates"), [](const QCborMap& m) { return QString::number(number(m, "candidates")); },
         [](const QCborMap& m) { return QVariant(number(m, "candidates")); }},
        {tr("Status"),
         [](const QCborMap& m) {
             return m.value(QStringLiteral("ended")).toBool() ? tr("Ended %1 ago").arg(ageText(number(m, "endedAgoMs")))
                                                               : tr("Active");
         },
         [](const QCborMap& m) {
             return QVariant(m.value(QStringLiteral("ended")).toBool() ? number(m, "endedAgoMs") + 1 : 0);
         }},
    };
}

std::uint64_t Ed3kKadLookupsModel::idAt(int r) const
{
    return r >= 0 && r < count() ? std::uint64_t(number(row(r), "id")) : 0;
}

int Ed3kKadLookupsModel::rowOf(std::uint64_t id) const
{
    for (int r = 0; r < count(); ++r)
        if (std::uint64_t(number(row(r), "id")) == id)
            return r;
    return -1;
}

QString Ed3kKadLookupsModel::kindText(int kind)
{
    switch (kind) {
    case 0: return tr("Nodes");
    case 1: return tr("Source");
    case 2: return tr("Keyword");
    case 3: return tr("Note");
    case 4: return tr("Set head");
    case 5: return tr("Set member");
    case 6: return tr("Live");
    case 7: return tr("Subscription");
    case 8: return tr("Hidden set");
    case 9: return tr("Part");
    case 10: return tr("Friend");
    case 11: return tr("Anonymous");
    case 12: return tr("Gate");
    case 13: return tr("Alias");
    case 14: return tr("Name");
    case 15: return tr("Name claim");
    default: return tr("Kind %1").arg(kind);
    }
}

// ---------------------------------------------------------------------------
// KadIncomingModel
// ---------------------------------------------------------------------------

KadIncomingModel::KadIncomingModel(Network network, QObject* parent)
    : Ed3kKadTableModel(parent)
    , m_network(network)
{
    const auto typeText = network == Network::Ed2k ? &KadIncomingModel::ed2kTypeText : &KadIncomingModel::ed3kTypeText;
    m_columns = {
        {tr("Time"),
         [](const QCborMap& m) {
             return QDateTime::fromMSecsSinceEpoch(number(m, "at")).toString(QStringLiteral("HH:mm:ss"));
         },
         [](const QCborMap& m) { return QVariant(number(m, "at")); }},
        {tr("Type"), [typeText](const QCborMap& m) { return typeText(int(number(m, "type"))); }, {}},
        {tr("Key"), [](const QCborMap& m) { return value(m, "target").toUpper(); }, {}},
        {tr("Name"), [](const QCborMap& m) { return value(m, "name"); }, {}},
        // flag + ip:port (or hostname); ed3k: flag + node ID, the address in its own IP column
        {tr("From"),
         [this, network](const QCborMap& m) {
             return network == Network::Ed3k ? value(m, "peer").toUpper() : addressText(m);
         },
         [this, network](const QCborMap& m) {
             return network == Network::Ed3k ? QVariant(value(m, "cc") + value(m, "peer")) : addressSort(m);
         }},
        {tr("Answer"), [](const QCborMap& m) { return answerText(m); }, {}},
    };
    const auto host = [](const QCborMap& m) { return splitEndpoint(value(m, "addr")).host; };
    if (network == Network::Ed3k) {
        m_columns.push_back({tr("IP"), [this](const QCborMap& m) { return addressText(m); },
                             [this](const QCborMap& m) { return addressSort(m); }});
        setHostnameColumn(ColIp, host, tr("Hostname / IP"));
    } else {
        setHostnameColumn(ColFrom, host);
    }
}

QVariant KadIncomingModel::data(const QModelIndex& index, int role) const
{
    if (index.isValid() && index.column() == ColFrom && index.row() < count()) {
        const QCborMap& m = row(index.row());
        if (role == Qt::DecorationRole && CountryFlags::showFlags())
            return CountryFlags::flag(value(m, "cc"));
        if (role == Qt::ToolTipRole) {
            const QString cc = CountryFlags::tooltip(value(m, "cc"));
            if (m_network == Network::Ed3k || value(m, "addr").isEmpty())
                return cc;
            const QString host = m_resolver.toolTip(hostAddress(m), splitEndpoint(value(m, "addr")).port);
            return cc.isEmpty() ? host : cc + QLatin1Char('\n') + host;
        }
    }
    if (role == Qt::ToolTipRole && index.isValid() && index.column() == ColIp && index.row() < count()
        && !value(row(index.row()), "addr").isEmpty())
        return m_resolver.toolTip(hostAddress(row(index.row())), splitEndpoint(value(row(index.row()), "addr")).port);
    return Ed3kKadTableModel::data(index, role);
}

void KadIncomingModel::setIncoming(const QCborArray& rows)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QCborArray stamped;
    for (const auto& v : rows) {
        QCborMap m = v.toMap();
        m.insert(QStringLiteral("at"), now - number(m, "agoMs"));
        stamped.append(m);
    }
    setRows(stamped);
    refreshHostnames();
}

std::uint64_t KadIncomingModel::idAt(int row) const
{
    return row >= 0 && row < count() ? static_cast<std::uint64_t>(number(this->row(row), "id")) : 0;
}

int KadIncomingModel::rowOf(std::uint64_t id) const
{
    for (int r = 0; r < count(); ++r)
        if (idAt(r) == id)
            return r;
    return -1;
}

int KadIncomingModel::newestWithContacts() const
{
    // rows come newest first
    for (int r = 0; r < count(); ++r)
        if (number(row(r), "contacts") > 0 && !value(row(r), "target").isEmpty())
            return r;
    return -1;
}

QString KadIncomingModel::titleAt(int r) const
{
    const QCborMap& m = row(r);
    const int type = int(number(m, "type"));
    const QString text = m_network == Network::Ed2k ? ed2kTypeText(type) : ed3kTypeText(type);
    const QString name = value(m, "name");
    return QStringLiteral("%1: %2").arg(text, name.isEmpty() ? value(m, "target").toUpper() : name);
}

QString KadIncomingModel::ed2kTypeText(int type)
{
    // kad::IncomingType
    switch (type) {
    case 0: return tr("Bootstrap");
    case 1: return tr("Node Lookup");
    case 2: return tr("Keyword Search");
    case 3: return tr("Source Search");
    case 4: return tr("Notes Search");
    case 5: return tr("Store Keyword");
    case 6: return tr("Store Source");
    case 7: return tr("Store Notes");
    case 8: return tr("Firewall Check");
    case 9: return tr("Find Buddy");
    case 10: return tr("Callback");
    default: return tr("Type %1").arg(type);
    }
}

QString KadIncomingModel::ed3kTypeText(int type)
{
    const QString kind = Ed3kKadLookupsModel::kindText(type % 100);
    switch (type / 100) {
    case 0: return tr("Node Lookup");
    case 1: return tr("Search %1").arg(kind);
    case 2: return tr("Store %1").arg(kind);
    default: return kind;
    }
}

QString KadIncomingModel::answerText(const QCborMap& m)
{
    if (!m.value(QStringLiteral("answered")).toBool()) {
        if (const qint64 refused = number(m, "refused"); refused > 0)
            return tr("Refused: %1").arg(refusalText(int(refused), int(number(m, "refusedArg"))));
        const qint64 error = number(m, "error");
        return error > 0 ? tr("Error %1").arg(error) : tr("Not answered");
    }
    QStringList parts;
    if (const qint64 n = number(m, "contacts"); n > 0)
        parts << tr("%n contact(s)", nullptr, int(n));
    const qint64 results = number(m, "results");
    const qint64 records = number(m, "records");
    // ed3k: a search or store at a node that is not responsible gets contacts only
    if (m.contains(QStringLiteral("responsible")) && !m.value(QStringLiteral("responsible")).toBool()
        && (results >= 0 || records >= 0)) {
        parts << tr("not responsible");
        return parts.join(QStringLiteral(", "));
    }
    if (results >= 0)
        parts << tr("%n result(s)", nullptr, int(results));
    if (records >= 0) {
        const qint64 stored = number(m, "stored");
        parts << (stored >= 0 && stored != records ? tr("%1 of %2 stored").arg(stored).arg(records)
                                                   : tr("%n stored", nullptr, int(records)));
    }
    if (const qint64 load = number(m, "load"); load >= 0)
        parts << tr("load %1%").arg(load);
    return parts.join(QStringLiteral(", "));
}

QString KadIncomingModel::refusalText(int code, int arg)
{
    // kad::IncomingRefusal
    switch (code) {
    case 1: return tr("malformed request");
    case 2: return tr("we are firewalled");
    case 3: return tr("we use a buddy ourselves");
    case 4: return tr("all %1 buddy slots taken").arg(arg);
    case 5: return tr("ourselves");
    case 6: return tr("firewall check running");
    case 7: return tr("last link silent, %n min cool-off", nullptr, arg);
    case 8: return tr("IP already served");
    case 9: return tr("already a known client");
    default: return tr("reason %1").arg(code);
    }
}

QString KadIncomingModel::addressText(const QCborMap& m) const
{
    return m_resolver.endpointText(hostAddress(m), splitEndpoint(value(m, "addr")).port);
}

QVariant KadIncomingModel::addressSort(const QCborMap& m) const
{
    return QVariant(m_resolver.displayText(hostAddress(m), endpointSortKey(value(m, "addr"))));
}

} // namespace eMule
