#pragma once

/// @file PortTest.h
/// @brief The connection test of the Connection page and the first-start wizard —
///        MFC TriggerPortTest (srchybrid/OtherFunctions.cpp:3105). Header-only.

#include "app/AppConfig.h"
#include "app/IpcClient.h"

#include "IpcMessage.h"

#include <QCborMap>
#include <QDesktopServices>
#include <QObject>
#include <QPointer>
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

} // namespace eMule::PortTest
