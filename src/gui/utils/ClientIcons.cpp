#include "pch.h"
/// @file ClientIcons.cpp
/// @brief Client software icons shared by the client lists and download sources.

#include "utils/ClientIcons.h"
#include "app/SkinIcons.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QStringList>

namespace eMule {

namespace {

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

} // namespace

QIcon clientSoftwareIcon(int softwareId, bool hasCredit, bool isFriend)
{
    static QHash<int, QIcon> cache;
    static quint64 skinSeen = 0;
    if (SkinIcons::instance().changedSince(skinSeen))
        cache.clear();   // friend composites are baked

    // URL source → Server icon (MFC index 15)
    if (softwareId == SoftURL) {
        static QIcon urlIcon = skinIcon(QStringLiteral(":/icons/Server.ico"));
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
        QIcon icon = skinIcon(path);
        cache.insert(key, icon);
        return icon;
    }

    // Composite: software icon + friend badge at bottom-right
    QPixmap pixmap = skinIcon(path).pixmap(16, 16);
    {
        QPainter painter(&pixmap);
        painter.drawPixmap(6, 6, 10, 10, skinIcon(QStringLiteral(":/icons/Friend.ico")).pixmap(10, 10));
    }
    QIcon composite(pixmap);
    cache.insert(key, composite);
    return composite;
}

Ed3kJoin ed3kJoinOf(const QString& tag)
{
    if (tag == QLatin1String("torrent"))
        return Ed3kJoin::Torrent;
    if (tag == QLatin1String("usenet"))
        return Ed3kJoin::Usenet;
    if (tag == QLatin1String("hoster"))
        return Ed3kJoin::Hoster;
    return Ed3kJoin::None;
}

QIcon ed3kClientIcon(bool anonymous, Ed3kJoin join)
{
    static QHash<int, QIcon> cache;
    static quint64 skinSeen = 0;
    if (SkinIcons::instance().changedSince(skinSeen))
        cache.clear();
    const int key = static_cast<int>(join) * 2 + (anonymous ? 1 : 0);
    if (auto it = cache.constFind(key); it != cache.cend())
        return it.value();

    const QIcon base = skinIcon(QStringLiteral(":/icons/ClientEd3k.ico"));
    if (!anonymous && join == Ed3kJoin::None) {
        cache.insert(key, base);
        return base;
    }

    QImage img = base.pixmap(16, 16).toImage().convertToFormat(QImage::Format_ARGB32);
    if (anonymous) {
        // our anonymous key: a dark grey shadow of the node icon
        for (int y = 0; y < img.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const int g = qGray(line[x]) * 3 / 5;
                line[x] = qRgba(g, g, g, qAlpha(line[x]));
            }
        }
    }
    QPixmap pixmap = QPixmap::fromImage(img);
    if (join != Ed3kJoin::None) {
        const QString badge = join == Ed3kJoin::Torrent  ? QStringLiteral(":/icons/Torrent.ico")
                              : join == Ed3kJoin::Usenet ? QStringLiteral(":/icons/Usenet.ico")
                                                         : QStringLiteral(":/icons/Hoster.ico");
        QPainter painter(&pixmap);
        painter.drawPixmap(6, 6, 10, 10, skinIcon(badge).pixmap(10, 10));
    }
    QIcon icon(pixmap);
    cache.insert(key, icon);
    return icon;
}

QString ed3kNetworkText(bool anonymous, Ed3kJoin join)
{
    QString text = anonymous ? QCoreApplication::translate("ClientIcons", "ed3k, anonymous key")
                             : QCoreApplication::translate("ClientIcons", "ed3k");
    switch (join) {
    case Ed3kJoin::Torrent: text = QCoreApplication::translate("ClientIcons", "%1, for a torrent").arg(text); break;
    case Ed3kJoin::Usenet:  text = QCoreApplication::translate("ClientIcons", "%1, for a Usenet download").arg(text); break;
    case Ed3kJoin::Hoster:  text = QCoreApplication::translate("ClientIcons", "%1, for a hoster download").arg(text); break;
    case Ed3kJoin::None:    break;
    }
    return text;
}

QString networkIconPath(Ed3kJoin network)
{
    switch (network) {
    case Ed3kJoin::Torrent: return QStringLiteral(":/icons/Torrent.ico");
    case Ed3kJoin::Usenet:  return QStringLiteral(":/icons/Usenet.ico");
    case Ed3kJoin::Hoster:  return QStringLiteral(":/icons/Hoster.ico");
    case Ed3kJoin::None:    break;
    }
    return {};
}

QIcon networkClientIcon(Ed3kJoin network)
{
    const QString path = networkIconPath(network);
    return path.isEmpty() ? QIcon() : skinIcon(path);
}

QString networkName(Ed3kJoin network)
{
    switch (network) {
    case Ed3kJoin::Torrent: return QStringLiteral("BitTorrent");
    case Ed3kJoin::Usenet:  return QStringLiteral("Usenet");
    case Ed3kJoin::Hoster:  return QCoreApplication::translate("ClientIcons", "Hoster");
    case Ed3kJoin::None:    break;
    }
    return {};
}

QString ed3kAliasText(const QCborMap& alias)
{
    const QString network = alias.value(QStringLiteral("network")).toString();
    const QString name = alias.value(QStringLiteral("name")).toString();
    const QString id = alias.value(QStringLiteral("id")).toString();
    if (network == QLatin1String("torrent"))
        return QCoreApplication::translate("ClientIcons", "Torrent: %1 (%2)").arg(name, id);
    if (network == QLatin1String("usenet"))
        return QCoreApplication::translate("ClientIcons", "Usenet: %1").arg(name);
    if (network == QLatin1String("hoster"))
        return QCoreApplication::translate("ClientIcons", "Hoster: %1 (%2)").arg(name, id);
    if (network == QLatin1String("ed2k"))
        return name.isEmpty() ? QCoreApplication::translate("ClientIcons", "eD2K: %1").arg(id)
                              : QCoreApplication::translate("ClientIcons", "eD2K: %1 (%2)").arg(name, id);
    return {};
}

QString ed3kJoinText(const QCborMap& join)
{
    const QString network = join.value(QStringLiteral("network")).toString();
    const QString state = join.value(QStringLiteral("state")).toString();
    const QString reason = join.value(QStringLiteral("reason")).toString();
    QString name;
    if (network == QLatin1String("ed2k"))
        name = QStringLiteral("eD2K");
    else if (network == QLatin1String("torrent"))
        name = networkName(Ed3kJoin::Torrent);
    else if (network == QLatin1String("usenet"))
        name = networkName(Ed3kJoin::Usenet);
    else if (network == QLatin1String("hoster"))
        name = networkName(Ed3kJoin::Hoster);
    else
        return {};
    QString text;
    if (state == QLatin1String("joined")) {
        QStringList jobs;
        for (const auto& j : join.value(QStringLiteral("jobs")).toArray())
            if (const QString t = ed3kAliasText(j.toMap()); !t.isEmpty())
                jobs << t;
        // was: "Joined" / "Joined (%1)" only; hops.md §1: the key is shown
        const bool anon = join.value(QStringLiteral("key")).toString() == QLatin1String("anon");
        if (anon)
            text = jobs.isEmpty() ? QCoreApplication::translate("ClientIcons", "Joined under the anonymous key")
                                  : QCoreApplication::translate("ClientIcons", "Joined under the anonymous key (%1)").arg(jobs.join(QStringLiteral("; ")));
        else
            text = jobs.isEmpty() ? QCoreApplication::translate("ClientIcons", "Joined")
                                  : QCoreApplication::translate("ClientIcons", "Joined (%1)").arg(jobs.join(QStringLiteral("; ")));
    } else if (state == QLatin1String("skipped")) {
        // hops.md §1: only ed3k hides; the other networks show this address
        // was: "Skipped (%1 is not anonymous; the file is under the anonymous key)"
        text = reason == QLatin1String("anonKey")
            ? QCoreApplication::translate("ClientIcons", "Skipped (the file is under the anonymous key; %1 is off under "
                                                         "Options › ed3k › Extended › Kad publishing)").arg(name)
            : QCoreApplication::translate("ClientIcons", "Skipped");
    } else if (state == QLatin1String("available")) {
        text = QCoreApplication::translate("ClientIcons", "Available, not joined");
    } else if (reason == QLatin1String("noEd2kHash")) {
        text = QCoreApplication::translate("ClientIcons", "Not available (no eD2K hash, hashed while eD2K was disabled)");
    } else {
        text = QCoreApplication::translate("ClientIcons", "Not available (network or its ed3k join is off)");
    }
    return QCoreApplication::translate("ClientIcons", "%1: %2").arg(name, text);
}

QString ed3kJoinsText(const QCborArray& joins)
{
    QStringList lines;
    for (const auto& j : joins)
        if (const QString t = ed3kJoinText(j.toMap()); !t.isEmpty())
            lines << t;
    return lines.join(u'\n');
}

QIcon i2pIcon()
{
    static const QIcon icon = skinIcon(QStringLiteral(":/icons/I2P.ico"));
    return icon;
}

SourceStateIcon sourceStateIconKind(const QString& stateToken, bool remoteQueueFull, bool a4af)
{
    if (a4af)
        return SourceStateIcon::NoNeededOrFull;
    if (stateToken == QLatin1String("Connected") || stateToken == QLatin1String("Connecting")
        || stateToken == QLatin1String("WaitCallback") || stateToken == QLatin1String("WaitCallbackKad")
        || stateToken == QLatin1String("TooManyConns") || stateToken == QLatin1String("TooManyConnsKad"))
        return SourceStateIcon::Connecting;
    if (stateToken == QLatin1String("OnQueue"))
        return remoteQueueFull ? SourceStateIcon::NoNeededOrFull : SourceStateIcon::OnQueue;
    if (stateToken == QLatin1String("Downloading") || stateToken == QLatin1String("ReqHashSet"))
        return SourceStateIcon::Downloading;
    if (stateToken == QLatin1String("NoNeededParts") || stateToken == QLatin1String("Error"))
        return SourceStateIcon::NoNeededOrFull;
    return SourceStateIcon::Unknown;
}

QIcon sourceStateIcon(SourceStateIcon kind)
{
    // emule.rc:1409-1413: SrcDownloading Client0, SrcConnecting Client1, SrcOnQueue
    // Client2, SrcNNPQF Client3, SrcUnknown Client4
    static const QIcon icons[] = {
        skinIcon(QStringLiteral(":/icons/Client0.ico")), skinIcon(QStringLiteral(":/icons/Client2.ico")),
        skinIcon(QStringLiteral(":/icons/Client1.ico")), skinIcon(QStringLiteral(":/icons/Client3.ico")),
        skinIcon(QStringLiteral(":/icons/Client4.ico"))};
    return icons[static_cast<int>(kind)];
}

QIcon iconPair(const QIcon& first, const QIcon& second)
{
    static QHash<QString, QIcon> cache;
    static quint64 skinSeen = 0;
    if (SkinIcons::instance().changedSince(skinSeen))
        cache.clear();
    const QString key = QStringLiteral("%1/%2").arg(first.cacheKey()).arg(second.cacheKey());
    if (const auto it = cache.constFind(key); it != cache.cend())
        return *it;

    constexpr int kSize = 16;
    constexpr int kStep = 20;
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(qRound((kStep + kSize) * dpr), qRound(kSize * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        first.paint(&p, QRect(0, 0, kSize, kSize));
        second.paint(&p, QRect(kStep, 0, kSize, kSize));
    }
    QIcon icon(pm);
    if (cache.size() > 500)
        cache.clear();
    cache.insert(key, icon);
    return icon;
}

std::optional<TransferNet> transferNetOf(Ed3kJoin network)
{
    switch (network) {
    case Ed3kJoin::Torrent: return TransferNet::Torrent;
    case Ed3kJoin::Usenet:  return TransferNet::Usenet;
    case Ed3kJoin::Hoster:  return TransferNet::Hoster;
    case Ed3kJoin::None:    break;
    }
    return std::nullopt;
}

QIcon transferNetsIcon(const QList<TransferNet>& nets)
{
    if (nets.isEmpty())
        return {};
    static QHash<QString, QIcon> cache;
    static quint64 skinSeen = 0;
    if (SkinIcons::instance().changedSince(skinSeen))
        cache.clear();
    QString key;
    for (const TransferNet n : nets)
        key += QChar(u'a' + static_cast<int>(n));
    if (const auto it = cache.constFind(key); it != cache.cend())
        return *it;

    const auto iconOf = [](TransferNet n) {
        switch (n) {
        case TransferNet::Ed2k:     return skinIcon(QStringLiteral(":/icons/ClientCompatible.ico"));
        case TransferNet::Ed3k:     return ed3kClientIcon();
        case TransferNet::Ed3kAnon: return ed3kClientIcon(true);
        case TransferNet::Torrent:  return networkClientIcon(Ed3kJoin::Torrent);
        case TransferNet::Usenet:   return networkClientIcon(Ed3kJoin::Usenet);
        case TransferNet::Hoster:   return networkClientIcon(Ed3kJoin::Hoster);
        case TransferNet::Public:   return skinIcon(QStringLiteral(":/icons/Global.ico"));
        case TransferNet::I2p:      return i2pIcon();
        }
        return QIcon();
    };
    constexpr int kSize = 16;
    constexpr int kStep = 18;   // 2 px apart
    constexpr int kDpr = 2;     // no widget here; Qt scales per screen
    const int width = kSize + static_cast<int>(nets.size() - 1) * kStep;
    QPixmap pm(QSize(width, kSize) * kDpr);
    pm.setDevicePixelRatio(kDpr);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        int x = 0;
        for (const TransferNet n : nets) {
            iconOf(n).paint(&p, QRect(x, 0, kSize, kSize));
            x += kStep;
        }
    }
    QIcon icon(pm);
    cache.insert(key, icon);
    return icon;
}

QString transferNetsText(const QList<TransferNet>& nets)
{
    QStringList names;
    for (const TransferNet n : nets) {
        switch (n) {
        case TransferNet::Ed2k:     names << QStringLiteral("eD2K"); break;
        case TransferNet::Ed3k:     names << QStringLiteral("ed3k"); break;
        case TransferNet::Ed3kAnon: names << QCoreApplication::translate("ClientIcons", "ed3k (anonymous key)"); break;
        case TransferNet::Torrent:  names << networkName(Ed3kJoin::Torrent); break;
        case TransferNet::Usenet:   names << networkName(Ed3kJoin::Usenet); break;
        case TransferNet::Hoster:   names << networkName(Ed3kJoin::Hoster); break;
        case TransferNet::Public:   names << QCoreApplication::translate("ClientIcons", "Public"); break;
        case TransferNet::I2p:      names << QStringLiteral("I2P"); break;
        }
    }
    return names.join(QStringLiteral(", "));
}

Ed3kJobSource Ed3kJobSource::fromCbor(const QCborMap& m)
{
    Ed3kJobSource s;
    s.key = m.value(QStringLiteral("userHash")).toString();
    s.name = m.value(QStringLiteral("userName")).toString();
    s.software = m.value(QStringLiteral("software")).toString();
    const QString addr = m.value(QStringLiteral("addr")).toString();
    const qint64 port = m.value(QStringLiteral("port")).toInteger();
    if (!addr.isEmpty())
        s.address = port > 0 ? (addr.contains(u':') ? QStringLiteral("[%1]:%2") : QStringLiteral("%1:%2")).arg(addr).arg(port)
                             : addr;
    s.cc = m.value(QStringLiteral("cc")).toString();
    s.fileName = m.value(QStringLiteral("fileName")).toString();
    s.state = m.value(QStringLiteral("downloadState")).toString();
    s.rate = m.value(QStringLiteral("datarate")).toInteger();
    s.received = m.value(QStringLiteral("transferredDown")).toInteger();
    s.have = int(m.value(QStringLiteral("availPartCount")).toInteger());
    s.pieces = int(m.value(QStringLiteral("partCount")).toInteger());
    for (const QCborValue& c : m.value(QStringLiteral("sourcePartMap")).toArray())
        s.parts.append(char(c.toInteger()));
    s.anonymous = m.value(QStringLiteral("key")).toString() == QLatin1String("Anonymous");
    s.join = ed3kJoinOf(m.value(QStringLiteral("join")).toString());
    return s;
}

QString Ed3kJobSource::addressText() const
{
    return address.isEmpty() ? QCoreApplication::translate("ClientIcons", "(hidden)") : address;
}

QString Ed3kJobSource::stateText() const
{
    return state == QLatin1String("Downloading") ? QCoreApplication::translate("ClientIcons", "Downloading")
                                                 : QCoreApplication::translate("ClientIcons", "On Queue");
}

QString Ed3kJobSource::toolTip() const
{
    QStringList lines{ed3kNetworkText(anonymous, join)};
    if (!software.isEmpty())
        lines << QCoreApplication::translate("ClientIcons", "Client: %1").arg(software);
    if (!fileName.isEmpty())
        lines << QCoreApplication::translate("ClientIcons", "File: %1").arg(fileName);
    return lines.join(u'\n');
}

} // namespace eMule
