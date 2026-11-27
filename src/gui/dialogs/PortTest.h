#pragma once

/// @file PortTest.h
/// @brief The connection test of the Connection page and the first-start wizard —
///        MFC TriggerPortTest (srchybrid/OtherFunctions.cpp:3105). Header-only.
///        Also the eD2K, BitTorrent and ed3k tests (NetworkTest::run): the daemon calls the
///        emule-qt.org API (TestPorts) and the answer is shown in a message box. eD2K falls
///        back to the browser page when the core or the website cannot run it.

#include "app/AppConfig.h"
#include "app/IpcClient.h"
#include "dialogs/VpnPromo.h"
#include "net/PortTestApi.h"

#include "IpcMessage.h"

#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

namespace eMule::PortTest {

/// The test page's address for these ports. The page can only see the family the
/// browser reaches it over, so our own public addresses are handed along for the
/// other one.
[[nodiscard]] inline QUrl url(int tcpPort, int udpPort, const QString& ipv4, const QString& ipv6)
{
    QUrl result(QLatin1String(kWebsiteUrl) + QLatin1String(kPortTestPath));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("tcpport"), QString::number(tcpPort));
    query.addQueryItem(QStringLiteral("udpport"), QString::number(udpPort));
    if (!ipv4.isEmpty())
        query.addQueryItem(QStringLiteral("ip4"), ipv4);
    if (!ipv6.isEmpty())
        query.addQueryItem(QStringLiteral("ip6"), ipv6);
    result.setQuery(query);
    return result;
}

/// Open the test in the browser. The daemon is asked for the public addresses: it
/// may run on another host than this GUI, whose own addresses would be the wrong ones.
/// Nothing happens once @p context is gone.
inline void open(IpcClient* ipc, QObject* context, int tcpPort, int udpPort)
{
    if (!ipc || !ipc->isConnected()) {
        QDesktopServices::openUrl(url(tcpPort, udpPort, {}, {}));
        return;
    }
    Ipc::IpcMessage req(Ipc::IpcMsgType::GetNetworkInfo);
    ipc->sendRequest(std::move(req), [guard = QPointer<QObject>(context), tcpPort, udpPort](
                                         const Ipc::IpcMessage& resp) {
        if (!guard)
            return;
        const QCborMap ed2k = resp.fieldMap(1).value(QStringLiteral("ed2k")).toMap();
        QDesktopServices::openUrl(url(tcpPort, udpPort,
                                      ed2k.value(QStringLiteral("publicIPv4")).toString(),
                                      ed2k.value(QStringLiteral("publicIPv6")).toString()));
    });
}

/// TestPorts field 0 (PortTestApi::Network in the core).
enum class Network : int {
    BitTorrent = 1,
    Ed3k = 2,
    Ed2k = 3,
};

/// The eD2K, BitTorrent and ed3k tests. A class only for tr() (context "PortTest").
class NetworkTest {
    Q_DECLARE_TR_FUNCTIONS(PortTest)

public:
    /// One verdict of the server (or of the ed3k node) in words.
    [[nodiscard]] static QString reasonText(const QCborMap& entry)
    {
        if (entry.value(QStringLiteral("open")).toBool())
            return tr("reachable");
        const QString reason = entry.value(QStringLiteral("reason")).toString();
        if (reason == QLatin1String("closed"))
            return tr("not reachable: closed, nothing is listening on this port");
        if (reason == QLatin1String("rejected"))
            return tr("not reachable: rejected by a firewall");
        if (reason == QLatin1String("filtered"))
            return tr("not reachable: no answer (firewall or missing port forwarding)");
        if (reason == QLatin1String("server_egress_blocked"))
            return tr("not tested: the test server may not send to this port");
        if (reason == QLatin1String("server_no_ipv6"))
            return tr("not tested: the test server cannot reach this IPv6 address");
        if (reason == QLatin1String("send_failed"))
            return tr("not tested: the test packet could not be sent");
        if (reason == QLatin1String("invalid_target"))
            return tr("not tested: the address is not public");
        if (reason == QLatin1String("rate_limited"))
            return tr("not tested: too many tests, try again in a minute");
        if (reason == QLatin1String("bad_reply"))
            return tr("not reachable: something else answers on this port");
        if (reason == QLatin1String("crypt_failed"))
            return tr("not reachable: the encrypted connection failed");
        if (reason == PortTestApi::kReasonDropped)
            return tr("not reachable: the core closed the connection at once (IP filter or ban)");
        if (reason == QLatin1String("udp_skipped"))
            return tr("not tested: TCP failed first");
        return tr("not reachable (%1)").arg(reason);
    }

    /// A TestPorts error (PortTestApi::Failure) in words; @p detail is English, from the core or server.
    [[nodiscard]] static QString failureText(int code, const QString& detail)
    {
        using PortTestApi::Failure;
        switch (static_cast<Failure>(code)) {
        case Failure::BtNotRunning:
            return tr("BitTorrent is not running, so its port cannot be tested.");
        case Failure::Ed3kNotRunning:
            return tr("The ed3k node is not running, so its port cannot be tested.");
        case Failure::NoProbe:
            return tr("The ed3k node could not make a test packet.");
        case Failure::NoAnswer:
            return tr("The test server emule-qt.org did not answer.") + QStringLiteral("\n\n") + detail;
        case Failure::Refused:
            return tr("The test server refused the test.") + QStringLiteral("\n\n") + detail;
        case Failure::OldServer:
            return tr("The test server cannot test this port yet. Please try again later.");
        case Failure::Ed2kNotListening:
            return tr("eD2K is not listening on a TCP port, so it cannot be tested.");
        case Failure::NoBoundAddress:
            return tr("The network interface selected in Options has no public address known yet, so nothing "
                      "could be tested over it. Connect to the network first, then test again.");
        case Failure::UnknownNetwork:
        default:
            return tr("The core did not run the test.") + (detail.isEmpty() ? QString() : QStringLiteral("\n\n") + detail);
        }
    }

    /// Whether a target has every port its network needs: TCP and UDP for BitTorrent and eD2K
    /// (eD2K TCP only with UDP off), UDP for ed3k. A port counts as open when the website
    /// reached it or peers prove it open at the tested address (#216).
    [[nodiscard]] static bool targetReachable(Network network, const QCborMap& target)
    {
        const bool udp = portOpen(target, QStringLiteral("udp"));
        if (network == Network::Ed3k)
            return udp;
        const bool tcp = portOpen(target, QStringLiteral("tcp"));
        if (network == Network::Ed2k && !target.contains(QStringLiteral("udp")))
            return tcp;
        return udp && tcp;
    }

    /// The message of a TestPorts result: the verdict first, then each address family.
    [[nodiscard]] static QString reportText(const QCborMap& result, bool* allReachable = nullptr)
    {
        const auto network = static_cast<Network>(result.value(QStringLiteral("network")).toInteger());
        const QCborArray targets = result.value(QStringLiteral("targets")).toArray();
        const qint64 udpPort = result.value(QStringLiteral("udpPort")).toInteger();

        bool all = !targets.isEmpty();
        bool any = false;
        bool lost = false;   // a test packet did not arrive at a port peers prove open
        bool missed = false; // ed3k: a test packet did not arrive, nothing proves the port open
        QStringList details;
        for (const QCborValue& v : targets) {
            const QCborMap t = v.toMap();
            const bool ok = targetReachable(network, t);
            all = all && ok;
            any = any || ok;
            for (const QString& port : {QStringLiteral("tcp"), QStringLiteral("udp")}) {
                const QCborMap e = t.value(port).toMap();
                if (!t.contains(port) || e.value(QStringLiteral("open")).toBool())
                    continue;
                lost = lost || proven(t, port);
                missed = missed
                         || (network == Network::Ed3k && !proven(t, port)
                             && e.value(QStringLiteral("reason")).toString() == PortTestApi::kReasonFiltered);
            }
            const QString family = t.value(QStringLiteral("family")).toInteger() == 6 ? QStringLiteral("IPv6") : QStringLiteral("IPv4");
            QString line = QStringLiteral("%1 (%2)").arg(family, t.value(QStringLiteral("ip")).toString());
            if (network != Network::Ed3k && t.contains(QStringLiteral("tcp")))
                line += QLatin1Char('\n') + tr("TCP: %1").arg(portText(network, t, QStringLiteral("tcp")));
            if (t.contains(QStringLiteral("udp")))
                line += QLatin1Char('\n') + tr("UDP: %1").arg(portText(network, t, QStringLiteral("udp")));
            if (!ok)
                line += otherRouteNote(t);
            details << line;
        }
        if (allReachable)
            *allReachable = all;

        QString head;
        if (network == Network::Ed2k) {
            const qint64 tcpPort = result.value(QStringLiteral("tcpPort")).toInteger();
            head = udpPort != 0 ? tr("eD2K TCP port %1, UDP port %2.").arg(tcpPort).arg(udpPort)
                                : tr("eD2K TCP port %1 (UDP is off).").arg(tcpPort);
            head += QStringLiteral("\n\n");
            if (all)
                head += tr("The ports are reachable: servers give you a High ID.");
            else if (any)
                head += tr("The ports are reachable over one address family only.");
            else
                head += tr("The ports are not reachable: servers give you a Low ID and Kad stays firewalled.");
        } else if (network == Network::BitTorrent) {
            head = tr("BitTorrent port %1. TCP and UDP must both be reachable from the internet.").arg(udpPort);
            head += QStringLiteral("\n\n");
            if (all)
                head += tr("Both TCP and UDP are reachable.");
            else if (any)
                head += tr("TCP and UDP are both reachable over one address family only.");
            else
                head += tr("TCP and UDP are not both reachable. Other peers can still be reached, but fewer can connect to you.");
        } else {
            head = tr("ed3k UDP port %1. Only UDP must be reachable: the ed3k node uses no TCP port.").arg(udpPort);
            head += QStringLiteral("\n\n");
            if (all)
                head += tr("UDP is reachable.");
            else if (any)
                head += tr("UDP is reachable over one address family only.");
            else
                head += tr("UDP is not reachable. The node then finds fewer peers and relies on hole punching.");
        }
        // say plainly whether test packets got lost, and what that means
        if (lost)
            head += QLatin1Char(' ')
                    + tr("Some test packets from the website did not arrive, but peers prove the port open: "
                         "the packets were lost on the way (often dropped by a VPN or provider), the port is not closed.");
        else if (missed)
            head += QLatin1Char(' ') + tr("The website's test packets did not arrive.");
        head += boundNote(result);
        head += filterNote(result, all);
        return head + QStringLiteral("\n\n") + details.join(QStringLiteral("\n\n"));
    }

    /// Runs the test of @p network on the daemon and shows the answer. @p button reads
    /// "Testing..." meanwhile; the port tested is the one the core uses now, not an unsaved one.
    /// @p fallback (eD2K: the browser page) runs instead of an error when the core or the
    /// website cannot run the test.
    static void run(IpcClient* ipc, QWidget* parent, QPushButton* button, Network network,
                    std::function<void()> fallback = {})
    {
        if ((!ipc || !ipc->isConnected()) && fallback) {
            fallback();
            return;
        }
        if (!ipc || !ipc->isConnected()) {
            QMessageBox::warning(parent, tr("Test Ports"), tr("Not connected to the core."));
            return;
        }
        const QString label = button ? button->text() : QString();
        if (button) {
            button->setEnabled(false);
            button->setText(tr("Testing..."));
        }
        Ipc::IpcMessage req(Ipc::IpcMsgType::TestPorts);
        req.append(static_cast<qint64>(network));
        ipc->sendRequest(std::move(req), [guard = QPointer<QWidget>(parent), btn = QPointer<QPushButton>(button), label,
                                          fallback = std::move(fallback)](const Ipc::IpcMessage& resp) {
            if (btn) {
                btn->setEnabled(true);
                btn->setText(label);
            }
            if (!guard)
                return;
            // an older core (unknown network) or website (no proto=ed2k)
            const auto code = static_cast<PortTestApi::Failure>(resp.fieldInt(0));
            if (fallback && resp.type() == Ipc::IpcMsgType::Error
                && (code == PortTestApi::Failure::UnknownNetwork || code == PortTestApi::Failure::OldServer)) {
                fallback();
                return;
            }
            if (resp.type() == Ipc::IpcMsgType::Error) {
                QMessageBox::warning(guard, tr("Test Ports"), failureText(int(resp.fieldInt(0)), resp.fieldString(1)));
                return;
            }
            if (!resp.fieldBool(0)) {
                QMessageBox::warning(guard, tr("Test Ports"), tr("The core did not run the test."));
                return;
            }
            bool all = false;
            const QString text = reportText(resp.fieldMap(1), &all);
            QMessageBox box(all ? QMessageBox::Information : QMessageBox::Warning, tr("Test Ports"), text,
                            QMessageBox::Ok, guard);
            // open ports: anonymity only
            VpnPromo::addTo(box, all ? VpnPromo::Pitch::Anonymity : VpnPromo::Pitch::HighId);
            box.exec();
        });
    }

private:
    /// Peers prove @p port ("tcp"/"udp") of @p target open at the tested address.
    [[nodiscard]] static bool proven(const QCborMap& target, const QString& port)
    {
        return target.value(QStringLiteral("peerSame")).toBool() && target.contains(proofKey(port));
    }

    [[nodiscard]] static QString proofKey(const QString& port)
    {
        return port == QLatin1String("tcp") ? QStringLiteral("peerTcp") : QStringLiteral("peerUdp");
    }

    [[nodiscard]] static bool portOpen(const QCborMap& target, const QString& port)
    {
        return target.value(port).toMap().value(QStringLiteral("open")).toBool() || proven(target, port);
    }

    /// Who proves a port open, by the proof's source: ed3k nodes, eD2K clients (Kad) or the server.
    [[nodiscard]] static QString proofText(const QString& source, const QString& address)
    {
        if (source == PortTestApi::kProofEd3k)
            return tr("other ed3k nodes reach this node at %1").arg(address);
        if (source == PortTestApi::kProofKad)
            return tr("eD2K clients reach this UDP port at %1 (Kad check passed)").arg(address);
        return tr("the eD2K server gave a High ID at %1").arg(address);
    }

    /// One port of a target in words: the website's result, or the peers' proof when the
    /// website did not get through.
    [[nodiscard]] static QString portText(Network network, const QCborMap& target, const QString& port)
    {
        const QCborMap entry = target.value(port).toMap();
        const bool filtered = entry.value(QStringLiteral("reason")).toString() == PortTestApi::kReasonFiltered;
        const int packets = int(entry.value(QStringLiteral("packets")).toInteger());
        if (!entry.value(QStringLiteral("open")).toBool() && proven(target, port)) {
            QString why;
            if (!filtered)
                why = tr("The website's test failed (%1).").arg(reasonText(entry));
            else if (port == QLatin1String("tcp"))
                why = tr("The website's test connection got no answer: it was dropped on the way.");
            else if (packets > 0)
                why = tr("None of the %n test packet(s) from the website arrived: lost on the way.", nullptr, packets);
            else
                why = tr("The website's test packet did not arrive: lost on the way.");
            const QString proof = proofText(target.value(proofKey(port)).toString(),
                                            target.value(QStringLiteral("peerAddress")).toString());
            return tr("reachable, %1.").arg(proof) + QLatin1Char(' ') + why;
        }
        if (network == Network::Ed3k && filtered && packets > 0)
            return tr("not reachable: none of the %n test packet(s) arrived (firewall or missing port forwarding)", nullptr, packets);
        return reasonText(entry);
    }

    /// A bound interface of one family (#246): the other family was left out on purpose.
    [[nodiscard]] static QString boundNote(const QCborMap& result)
    {
        const qint64 families = result.value(QStringLiteral("families")).toInteger();
        if (families != PortTestApi::kSeenIPv4 && families != PortTestApi::kSeenIPv6)
            return {};
        const QString name = result.value(QStringLiteral("bindInterface")).toString();
        return QLatin1Char(' ')
               + (families == PortTestApi::kSeenIPv4
                      ? tr("Only IPv4 was tested: the network interface %1 selected in Options has no IPv6.").arg(name)
                      : tr("Only IPv6 was tested: the network interface %1 selected in Options has no IPv4.").arg(name));
    }

    /// eD2K (#266): the IP filter rejected peers during a failed test (likely the test server),
    /// or blocks the test server, which was let through for the test.
    [[nodiscard]] static QString filterNote(const QCborMap& result, bool allReachable)
    {
        QString note;
        const auto list = [](const QCborArray& entries) {
            QStringList out;
            for (const QCborValue& v : entries) {
                const QCborMap m = v.toMap();
                const QString why = m.value(QStringLiteral("description")).toString();
                const QString ip = m.value(QStringLiteral("address")).toString();
                out << (why.isEmpty() ? ip : QStringLiteral("%1 (%2)").arg(ip, why));
            }
            return out.join(QStringLiteral(", "));
        };
        const QCborArray hits = result.value(QStringLiteral("filterHits")).toArray();
        if (!allReachable && !hits.isEmpty())
            note += QLatin1Char(' ')
                    + tr("Your IP filter rejected %1 during the test, likely the test server: the result may be wrong. "
                         "Lower the IP filter level or allow that address, then test again.").arg(list(hits));
        const QCborArray blocked = result.value(QStringLiteral("serverFiltered")).toArray();
        if (!blocked.isEmpty())
            note += QLatin1Char(' ')
                    + tr("Your IP filter blocks the test server %1. It was let through for this test only.").arg(list(blocked));
        return note;
    }

    /// A failed target peers see at another address (#216): the test likely took another route.
    [[nodiscard]] static QString otherRouteNote(const QCborMap& target)
    {
        const QString peer = target.value(QStringLiteral("peerAddress")).toString();
        if (peer.isEmpty() || target.value(QStringLiteral("peerSame")).toBool())
            return {};
        return QLatin1Char('\n')
               + tr("Peers see this node at %1, not at the tested address, so this result does not apply to it. "
                    "The test request likely took another route (VPN, proxy or a second internet line).").arg(peer);
    }
};

} // namespace eMule::PortTest
