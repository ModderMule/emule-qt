/// @file HosterIcons.cpp
/// @brief Hoster favicons and account plugins — implementation.

#include "pch.h"
#include "utils/HosterIcons.h"

#include "app/IpcClient.h"
#include "utils/CountryFlags.h"
#include "IpcMessage.h"

#include <QCborMap>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

/// icons asked for in one request (the core takes 256)
constexpr qsizetype kAskBatch = 64;

} // namespace

HosterIcons* HosterIcons::of(IpcClient* ipc)
{
    if (!ipc)
        return nullptr;
    if (auto* c = ipc->findChild<HosterIcons*>(QString(), Qt::FindDirectChildrenOnly))
        return c;
    return new HosterIcons(ipc);
}

QIcon HosterIcons::icon(const QString& raw)
{
    const QString host = raw.trimmed().toLower();
    if (host.isEmpty())
        return {};
    if (const auto it = m_icons.constFind(host); it != m_icons.cend())
        return *it;
    if (!m_asked.contains(host) && m_ipc->isConnected()) {
        m_asked.insert(host);
        m_toAsk << host;
        if (!m_askTimer.isActive())
            m_askTimer.start();
    }
    return standIn(host);
}

QIcon HosterIcons::iconWithFlag(const QString& host)
{
    const QIcon base = icon(host);
    return base.isNull() ? base : CountryFlags::withFlag(base, country(host));
}

const QList<HosterIcons::AccountHost>& HosterIcons::accountHosts()
{
    if (!m_hostsAsked && m_ipc->isConnected()) {
        m_hostsAsked = true;
        m_asking = true;
        QPointer<HosterIcons> self(this);
        m_ipc->sendRequest(IpcMessage(IpcMsgType::GetHosterAccountHosts), [self](const IpcMessage& r) {
            if (!self)
                return;
            const QCborMap m = r.isValid() && r.fieldBool(0) ? r.fieldMap(1) : QCborMap{};
            self->m_asking = m.value(QStringLiteral("asking")).toBool();
            self->applyHosts(m.value(QStringLiteral("hosts")).toArray());
        });
    }
    return m_hosts;
}

QString HosterIcons::abbreviation(const QString& host)
{
    QString name = host.toUpper();
    if (const qsizetype dot = name.lastIndexOf(u'.'); dot > 0)
        name.truncate(dot);
    // consonants only (JD drops J too)
    static const QRegularExpression drop(QStringLiteral("[\\d\\WEIOAJU_]"));
    name.remove(drop);
    if (name.isEmpty())
        return host.left(2).toUpper();
    return QString(name.at(0)) + name.at(name.size() / 2);
}

// ---- private ----

HosterIcons::HosterIcons(IpcClient* ipc)
    : QObject(ipc)
    , m_ipc(ipc)
{
    m_askTimer.setSingleShot(true);
    m_askTimer.setInterval(50);
    connect(&m_askTimer, &QTimer::timeout, this, &HosterIcons::sendIconAsks);
    connect(ipc, &IpcClient::hosterCatalogChanged, this, &HosterIcons::onPush);
    // another core may know other plugins and icons
    connect(ipc, &IpcClient::connected, this, &HosterIcons::reset);
}

void HosterIcons::onPush(const IpcMessage& msg)
{
    const QCborMap m = msg.fieldMap(0);
    if (m.contains(QStringLiteral("hosts"))) {
        m_asking = false;
        applyHosts(m.value(QStringLiteral("hosts")).toArray());
    }
    QHash<QString, QByteArray> icons;
    for (const auto& [k, v] : m.value(QStringLiteral("icons")).toMap())
        icons.insert(k.toString(), v.toByteArray());
    applyIcons(icons);
    applyCountries(m.value(QStringLiteral("countries")).toMap());
}

void HosterIcons::applyIcons(const QHash<QString, QByteArray>& icons)
{
    for (const auto& [host, png] : icons.asKeyValueRange()) {
        QPixmap pm;
        if (png.isEmpty() || !pm.loadFromData(png, "PNG"))
            m_icons.insert(host, standIn(host));   // none: the stand-in stays
        else
            m_icons.insert(host, QIcon(pm));
        emit iconChanged(host);
    }
}

void HosterIcons::applyCountries(const QCborMap& countries)
{
    for (const auto& [k, v] : countries) {
        const QString host = k.toString();
        const QString cc = v.toString().toLower();
        if (host.isEmpty() || (m_countries.contains(host) && m_countries.value(host) == cc))
            continue;
        m_countries.insert(host, cc);
        emit iconChanged(host);
    }
}

void HosterIcons::applyHosts(const QCborArray& hosts)
{
    QList<AccountHost> list;
    for (const QCborValue& v : hosts) {
        const QCborMap h = v.toMap();
        AccountHost a{h.value(QStringLiteral("host")).toString(), h.value(QStringLiteral("name")).toString(),
                      h.value(QStringLiteral("multi")).toBool(), h.value(QStringLiteral("solver")).toBool()};
        if (!a.host.isEmpty())
            list.append(a);
    }
    // an empty answer keeps what is known (the core asks its plugins again)
    if (!list.isEmpty())
        m_hosts = std::move(list);
    emit accountHostsChanged();
}

void HosterIcons::sendIconAsks()
{
    while (!m_toAsk.isEmpty()) {
        const QStringList batch = m_toAsk.mid(0, kAskBatch);
        m_toAsk.remove(0, batch.size());
        IpcMessage req(IpcMsgType::GetHosterFavicons);
        req.append(QCborArray::fromStringList(batch));
        QPointer<HosterIcons> self(this);
        m_ipc->sendRequest(std::move(req), [self](const IpcMessage& r) {
            if (!self || !r.isValid() || !r.fieldBool(0))
                return;
            QHash<QString, QByteArray> icons;
            const QCborMap m = r.fieldMap(1);
            for (const auto& [k, v] : m.value(QStringLiteral("icons")).toMap())
                icons.insert(k.toString(), v.toByteArray());
            self->applyIcons(icons);
            self->applyCountries(m.value(QStringLiteral("countries")).toMap());
        });
    }
}

void HosterIcons::reset()
{
    m_icons.clear();
    m_countries.clear();
    m_asked.clear();
    m_toAsk.clear();
    m_hostsAsked = false;
    m_asking = false;
}

QIcon HosterIcons::standIn(const QString& host)
{
    static QHash<QString, QIcon> cache;
    if (const auto it = cache.constFind(host); it != cache.cend())
        return *it;
    QIcon icon;
    for (const int side : {16, 32}) {
        QPixmap pm(side, side);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        // a pastel per site, so neighbours differ
        const QColor bg = QColor::fromHsl(int(qHash(host) % 360), 140, 215);
        QPainterPath path;
        path.addRoundedRect(QRectF(0.5, 0.5, side - 1, side - 1), side / 5.0, side / 5.0);
        p.fillPath(path, bg);
        p.setPen(bg.darker(140));
        p.drawPath(path);
        QFont f;
        f.setBold(true);
        f.setPixelSize(side * 9 / 16);
        p.setFont(f);
        p.setPen(Qt::black);
        p.drawText(pm.rect(), Qt::AlignCenter, abbreviation(host));
        p.end();
        icon.addPixmap(pm);
    }
    cache.insert(host, icon);
    return icon;
}

} // namespace eMule
