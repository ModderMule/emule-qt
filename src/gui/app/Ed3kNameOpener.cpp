#include "pch.h"
#include "app/Ed3kNameOpener.h"

#include "app/IpcClient.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCborArray>
#include <QDateTime>
#include <QDesktopServices>
#include <QLocale>
#include <QMessageBox>
#include <QUrl>

namespace eMule {

namespace {

// ED3K_NAME_HOW_* (packmule ed3k.h; the GUI does not include the module)
enum How : int {
    HowOwn = 1,
    HowPetname = 2,
    HowPinned = 3,
    HowList = 4,
    HowFriends = 5,
    HowNetwork = 6,
    HowDomainVerified = 7,
    HowDnsUnverified = 8,
    HowKey = 9,
};
// ED3K_NAME_WHY_*
enum Why : int { WhyNoSuch = 1, WhyNoAnswer = 2, WhyZone = 3, WhyNoLink = 4 };

} // namespace

Ed3kNameOpener::Ed3kNameOpener(IpcClient* ipc, LinkOpener openLink, QObject* parent)
    : QObject(parent)
    , m_ipc(ipc)
    , m_openLink(std::move(openLink))
{
    if (ipc) {
        connect(ipc, &IpcClient::ed3kNameResolved, this, &Ed3kNameOpener::onResolved);
        connect(ipc, &IpcClient::ed3kNameFailed, this, &Ed3kNameOpener::onFailed);
        connect(ipc, &IpcClient::ed3kNameFriendListOffered, this, &Ed3kNameOpener::onFriendList);
    }
}

void Ed3kNameOpener::open(const QString& text)
{
    if (!m_ipc) {
        logWarning(QStringLiteral("Cannot open the ed3k link, no core connection: %1").arg(text));
        return;
    }
    m_ipc->resolveEd3kName(text, [self = QPointer<Ed3kNameOpener>(this), text](const Ipc::IpcMessage& reply) {
        if (!self)
            return;
        const QCborMap data = reply.field(1).toMap();
        if (!reply.fieldBool(0)) {
            logWarning(QStringLiteral("ed3k name not resolved (error %1): %2")
                           .arg(data.value(QStringLiteral("error")).toInteger())
                           .arg(text));
            return;
        }
        self->m_waiting.insert(data.value(QStringLiteral("id")).toInteger());
    });
}

QString Ed3kNameOpener::howText(const QCborMap& r)
{
    const qint64 a = r.value(QStringLiteral("howA")).toInteger();
    const qint64 b = r.value(QStringLiteral("howB")).toInteger();
    switch (int(r.value(QStringLiteral("how")).toInteger())) {
    case HowOwn:
    case HowPetname:
        return tr("yours");
    case HowPinned:
        return tr("pinned on %1").arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(a).date(), QLocale::ShortFormat));
    case HowList: {
        const QString title = r.value(QStringLiteral("howTitle")).toString();
        return title.isEmpty() ? tr("a name list") : tr("the list \"%1\"").arg(title);
    }
    case HowFriends:
        return tr("%1 of %2 friends").arg(a).arg(b);
    case HowNetwork:
        return tr("network, %1 sites").arg(a);
    case HowDomainVerified:
        return tr("domain-verified");
    case HowDnsUnverified:
        return tr("DNS, unverified");
    case HowKey:
        return tr("key %1").arg(zoneText(r.value(QStringLiteral("zone")).toString()));
    default:
        return tr("unknown");
    }
}

QString Ed3kNameOpener::zoneText(const QString& hex)
{
    const QByteArray raw = QByteArray::fromHex(hex.toLatin1());
    const auto* p = reinterpret_cast<const uint8*>(raw.constData());
    return encodeBase32(std::span<const uint8>(p, std::size_t(raw.size()))).toLower().left(8);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kNameOpener::onResolved(const Ipc::IpcMessage& msg)
{
    const QCborMap r = msg.field(0).toMap();
    const qint64 id = r.value(QStringLiteral("id")).toInteger();
    // ours, or a name typed in the browser that needs the user's yes
    if (!m_waiting.remove(id) && !r.value(QStringLiteral("viaSite")).toBool())
        return;
    if (confirm(r))
        openResolved(id);
}

void Ed3kNameOpener::onFailed(const Ipc::IpcMessage& msg)
{
    const QCborMap r = msg.field(0).toMap();
    if (!m_waiting.remove(r.value(QStringLiteral("id")).toInteger()))
        return;
    const QString name = r.value(QStringLiteral("name")).toString();
    QString why;
    switch (int(r.value(QStringLiteral("why")).toInteger())) {
    case WhyNoSuch: why = tr("The name does not exist in its zone."); break;
    case WhyNoAnswer: why = tr("No source names a zone for it."); break;
    case WhyZone: why = tr("It passes too many zones."); break;
    case WhyNoLink: why = tr("Neither DNS nor the web server names a usable link."); break;
    default: why = tr("It could not be resolved."); break;
    }
    const QCborArray ties = r.value(QStringLiteral("ties")).toArray();
    if (!ties.isEmpty()) {
        QStringList zones;
        for (const auto& t : ties)
            zones << zoneText(t.toString());
        why += QLatin1Char('\n') + tr("Sources tie between the zones %1.").arg(zones.join(QStringLiteral(", ")));
    }
    QMessageBox::warning(m_parent, tr("ed3k name"), tr("%1 could not be opened.\n\n%2").arg(name, why));
}

bool Ed3kNameOpener::confirm(const QCborMap& r)
{
    const int how = int(r.value(QStringLiteral("how")).toInteger());
    const QString name = r.value(QStringLiteral("name")).toString();
    const QString zone = r.value(QStringLiteral("zone")).toString();
    const QCborArray warnings = r.value(QStringLiteral("warnings")).toArray();
    const QCborArray ties = r.value(QStringLiteral("ties")).toArray();
    const bool askFirst = r.value(QStringLiteral("confirm")).toBool() || how == HowDnsUnverified;
    QStringList lines;
    lines << tr("%1 is the zone %2 (%3).").arg(name, zoneText(zone), howText(r));
    if (r.value(QStringLiteral("confirm")).toBool())
        lines << tr("Only the network knows this name: no friend and no list you follow knows it.");
    if (how == HowDnsUnverified)
        lines << tr("It comes from DNS only and is not verified by the domain.");
    if (r.value(QStringLiteral("webDisagree")).toBool())
        lines << tr("The domain's DNS record and its ed3k.json name different keys; ed3k.json decides.");
    for (const auto& w : warnings) {
        const QCborMap m = w.toMap();
        lines << tr("Warning: %1 names another zone, %2, than %3.").arg(howText(m), zoneText(m.value(QStringLiteral("zone")).toString()),
                                                                          zoneText(zone));
    }
    if (!ties.isEmpty()) {
        QStringList zones;
        for (const auto& t : ties)
            zones << zoneText(t.toString());
        lines << tr("Sources tie between the zones %1.").arg(zones.join(QStringLiteral(", ")));
    }
    // a name already pinned, own or under a key opens at once unless something is to be told
    const bool known = how == HowOwn || how == HowPetname || how == HowPinned || how == HowKey;
    if (known && !askFirst && warnings.isEmpty() && ties.isEmpty())
        return true;
    lines << QString() << tr("Your friends learn the names you open.") << tr("Open it?");
    const auto answer = QMessageBox::question(m_parent, tr("ed3k name"), lines.join(QLatin1Char('\n')), QMessageBox::Open | QMessageBox::Cancel,
                                              askFirst ? QMessageBox::Cancel : QMessageBox::Open);
    return answer == QMessageBox::Open;
}

void Ed3kNameOpener::openResolved(qint64 id)
{
    if (!m_ipc)
        return;
    m_ipc->openEd3kName(id, [self = QPointer<Ed3kNameOpener>(this)](const Ipc::IpcMessage& reply) {
        if (!self)
            return;
        const QCborMap data = reply.field(1).toMap();
        if (!reply.fieldBool(0)) {
            logWarning(QStringLiteral("ed3k name not opened (error %1)").arg(data.value(QStringLiteral("error")).toInteger()));
            return;
        }
        // a website: the browser at its local origin (name.md §6.2)
        const QString url = data.value(QStringLiteral("url")).toString();
        if (!url.isEmpty()) {
            QDesktopServices::openUrl(QUrl(url));
            return;
        }
        for (const auto& l : data.value(QStringLiteral("links")).toArray()) {
            const QString link = l.toString();
            if (link.startsWith(QLatin1String("ed3k:name"), Qt::CaseInsensitive))
                continue;   // the core followed it already
            if (link.startsWith(QLatin1String("ed3k:friend?"), Qt::CaseInsensitive) && self->m_ipc) {
                Ipc::IpcMessage add(Ipc::IpcMsgType::AddEd3kFriend);
                add.append(link);
                add.append(QString());
                self->m_ipc->sendRequest(std::move(add));
                continue;
            }
            if (self->m_openLink)
                self->m_openLink(link);
        }
    });
}

void Ed3kNameOpener::onFriendList(const Ipc::IpcMessage& msg)
{
    // name.md §4.5: a friend publishes a name list; following it is the user's choice
    const QString node = msg.fieldString(0);
    const QString link = msg.fieldString(1);
    if (!m_ipc || link.isEmpty() || m_offered.contains(link))
        return;
    m_offered.insert(link);
    auto* box = new QMessageBox(QMessageBox::Question, tr("ed3k name list"),
                                tr("Your ed3k friend %1 publishes a name list. Follow it?\n\nIts names are then "
                                   "resolved after the lists you already follow.")
                                    .arg(node.left(8)),
                                QMessageBox::Yes | QMessageBox::No, m_parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowModality(Qt::NonModal);
    box->setDefaultButton(QMessageBox::No);
    connect(box, &QMessageBox::finished, this, [this, box, link, node](int) {
        if (box->clickedButton() != box->button(QMessageBox::Yes) || !m_ipc)
            return;
        m_ipc->followEd3kNameList(link, tr("Friend %1").arg(node.left(8)), [](const Ipc::IpcMessage& reply) {
            if (!reply.fieldBool(0))
                logWarning(QStringLiteral("ed3k: the friend's name list was not followed"));
        });
    });
    box->show();
}

} // namespace eMule
