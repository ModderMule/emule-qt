#pragma once

/// @file PortTest.h
/// @brief The connection test of the Connection page and the first-start wizard —
///        MFC TriggerPortTest (srchybrid/OtherFunctions.cpp:3105). Header-only.
///        Also the BitTorrent and ed3k tests (runNetworkTest): the daemon calls the
///        emule-qt.org API (TestPorts) and the answer is shown in a message box.

#include "app/AppConfig.h"
#include "app/IpcClient.h"
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
};

/// The BitTorrent and ed3k tests. A class only for tr() (context "PortTest").
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
        case Failure::UnknownNetwork:
        default:
            return tr("The core did not run the test.") + (detail.isEmpty() ? QString() : QStringLiteral("\n\n") + detail);
        }
    }

    /// Whether a target has every port its network needs: TCP and UDP for BitTorrent, UDP for ed3k.
    [[nodiscard]] static bool targetReachable(Network network, const QCborMap& target)
    {
        const bool udp = target.value(QStringLiteral("udp")).toMap().value(QStringLiteral("open")).toBool();
        if (network == Network::Ed3k)
            return udp;
        return udp && target.value(QStringLiteral("tcp")).toMap().value(QStringLiteral("open")).toBool();
    }

    /// The message of a TestPorts result: the verdict first, then each address family.
    [[nodiscard]] static QString reportText(const QCborMap& result, bool* allReachable = nullptr)
    {
        const auto network = static_cast<Network>(result.value(QStringLiteral("network")).toInteger());
        const QCborArray targets = result.value(QStringLiteral("targets")).toArray();
        const qint64 udpPort = result.value(QStringLiteral("udpPort")).toInteger();

        bool all = !targets.isEmpty();
        bool any = false;
        QStringList details;
        for (const QCborValue& v : targets) {
            const QCborMap t = v.toMap();
            const bool ok = targetReachable(network, t);
            all = all && ok;
            any = any || ok;
            const QString family = t.value(QStringLiteral("family")).toInteger() == 6 ? QStringLiteral("IPv6") : QStringLiteral("IPv4");
            QString line = QStringLiteral("%1 (%2)").arg(family, t.value(QStringLiteral("ip")).toString());
            if (network == Network::BitTorrent && t.contains(QStringLiteral("tcp")))
                line += QLatin1Char('\n') + tr("TCP: %1").arg(reasonText(t.value(QStringLiteral("tcp")).toMap()));
            if (t.contains(QStringLiteral("udp")))
                line += QLatin1Char('\n') + tr("UDP: %1").arg(reasonText(t.value(QStringLiteral("udp")).toMap()));
            details << line;
        }
        if (allReachable)
            *allReachable = all;

        QString head;
        if (network == Network::BitTorrent) {
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
        return head + QStringLiteral("\n\n") + details.join(QStringLiteral("\n\n"));
    }

    /// Runs the test of @p network on the daemon and shows the answer. @p button reads
    /// "Testing..." meanwhile; the port tested is the one the core uses now, not an unsaved one.
    static void run(IpcClient* ipc, QWidget* parent, QPushButton* button, Network network)
    {
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
        ipc->sendRequest(std::move(req), [guard = QPointer<QWidget>(parent), btn = QPointer<QPushButton>(button), label](
                                             const Ipc::IpcMessage& resp) {
            if (btn) {
                btn->setEnabled(true);
                btn->setText(label);
            }
            if (!guard)
                return;
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
            box.exec();
        });
    }
};

} // namespace eMule::PortTest
