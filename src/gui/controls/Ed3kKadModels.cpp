#include "pch.h"
#include "controls/Ed3kKadModels.h"

#include "utils/CountryFlags.h"
#include "app/SkinIcons.h"

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

} // namespace

// ---------------------------------------------------------------------------
// Ed3kKadTableModel
// ---------------------------------------------------------------------------

Ed3kKadTableModel::Ed3kKadTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int Ed3kKadTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int Ed3kKadTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_columns.size());
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
    return m_columns[static_cast<size_t>(section)].title;
}

void Ed3kKadTableModel::setRows(const QCborArray& rows)
{
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(static_cast<size_t>(rows.size()));
    for (const auto& v : rows)
        m_rows.push_back(v.toMap());
    endResetModel();
}

void Ed3kKadTableModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
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
             return s > 0 ? ageText(s * 1000) : QString();
         },
         [](const QCborMap& m) { return QVariant(number(m, "checkInS")); }},
    };
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
    if (role == Qt::ToolTipRole && index.isValid() && index.row() < count()) {
        const QCborMap& m = row(index.row());
        if (index.column() == ColCountry)
            return CountryFlags::tooltip(value(m, "cc"));
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
        {tr("Target"), [](const QCborMap& m) { return value(m, "target"); }, {}},
        {tr("Kind"),
         [](const QCborMap& m) {
             const QString kind = kindText(int(number(m, "kind")));
             return m.value(QStringLiteral("store")).toBool() ? tr("Store %1").arg(kind) : kind;
         },
         {}},
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

} // namespace eMule
