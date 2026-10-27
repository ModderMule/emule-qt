#pragma once

/// @file Ed3kLan.h
/// @brief LAN discovery of mode A (lan-discovery.md; socket-modes.md "Implementer note (LAN
/// discovery)"): the multicast sockets are eMuleQt's, the extension is the engine's.
///
/// - Two QUdpSockets on port 6533 (AnyIPv4 / AnyIPv6, shared with other nodes of the host),
///   hop limit 1, multicast loop on; joined to 239.255.101.51 / ff02::6533:6b on every
///   interface that is up, can multicast, is not loopback or point-to-point (VPN tunnels) or
///   Apple's awdl / llw, and has a private, link-local or unique-local address; only the bound
///   one when the bindAddress pref selects one.
/// - Each such interface goes to the engine (lanInterface) with its networks; looked at again
///   every minute (lanInterfaceDown when one goes).
/// - Arrivals → lanDatagramIn with their interface; Ed3kSession::pump() calls drain(), which
///   sends what lanDatagramOut hands out on the interface it names.
/// - §4 per network: Qt knows no network category; the pref ed3k.lan is on or off for all.
///   ToDo: the Network List Manager category on Windows (packmule_udp::lan::category).

#include "Ed3kFfi.h"

#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

class QNetworkInterface;
class QUdpSocket;

namespace eMule::ed3k {

class Ed3kSession;

class Ed3kLan : public QObject {
    Q_OBJECT

public:
    /// The group port (lan-discovery.md §1).
    static constexpr std::uint16_t kPort = 6533;

    /// An interface LAN discovery runs on: its index, name and networks (address, prefix).
    struct Iface {
        std::uint32_t index = 0;
        QString name;
        std::vector<std::pair<QHostAddress, int>> nets;
        friend bool operator==(const Iface&, const Iface&) = default;
    };
    /// Tests: interfaces and the send path by hand; no sockets are opened then.
    struct Hooks {
        std::function<std::vector<Iface>()> interfaces;
        std::function<void(std::uint32_t iface, const QHostAddress& to, std::uint16_t port, const QByteArray& data)> send;
    };

    /// @p nodePort: the node's UDP port, announced on each interface.
    Ed3kLan(Ed3kSession& session, std::uint16_t nodePort, Hooks hooks = {});
    ~Ed3kLan() override;

    /// Sockets, groups and interfaces; lanEnable on. false: no socket could be bound (the
    /// caller turns LAN off).
    bool start();
    void stop();
    /// The engine died (maybe inside a socket's slot): off without touching it, the sockets
    /// freed later.
    void detach();
    /// What lanDatagramOut hands out, sent now. After each pump.
    void drain();
    /// A datagram of a multicast socket (or of a test) on interface @p iface.
    void datagramIn(std::uint32_t iface, const QHostAddress& from, std::uint16_t port, const QByteArray& data);
    /// The interfaces the engine was told of.
    [[nodiscard]] std::vector<std::uint32_t> interfacesUp() const;

    /// The interfaces of the system LAN discovery may run on (see the file comment).
    [[nodiscard]] static std::vector<Iface> systemInterfaces();
    /// 10/8, 172.16/12, 192.168/16, 169.254/16, fe80::/10, fc00::/7.
    [[nodiscard]] static bool isLocalNet(const QHostAddress& ip);

private:
    void onReadyRead(QUdpSocket* socket);
    void rescan();
    void sendOne(std::uint32_t iface, const QHostAddress& to, std::uint16_t port, const QByteArray& data);
    [[nodiscard]] static bool eligible(const QNetworkInterface& iface);

    Ed3kSession& m_session;
    std::uint16_t m_nodePort = 0;
    Hooks m_hooks;
    std::unique_ptr<QUdpSocket> m_sock4;
    std::unique_ptr<QUdpSocket> m_sock6;
    std::map<std::uint32_t, Iface> m_up;
    QTimer m_rescan;
    bool m_started = false;
};

} // namespace eMule::ed3k
