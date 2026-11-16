#pragma once

/// @file UdpSocketSet.h
/// @brief The UDP sockets of one port: the wildcard (or a literal), or one per address
/// of the selected interface (#190).
///
/// Stands in for one QUdpSocket: datagrams of every socket come out of receiveDatagram(),
/// a send picks the socket by its sender (when bound) or the destination's family. The
/// first socket of a family is that family's sender, so listenAddresses() puts the
/// advertised address first.

#include "net/ListenAddressSet.h"

#include <QAbstractSocket>
#include <QHash>
#include <QNetworkDatagram>
#include <QObject>
#include <QVariant>

#include <memory>
#include <mutex>
#include <vector>

class QUdpSocket;

namespace eMule {

class UdpSocketSet : public QObject, public ListenAddressSet {
    Q_OBJECT

public:
    /// @p name names the set in the log ("client UDP").
    explicit UdpSocketSet(QString name, QObject* parent = nullptr);
    ~UdpSocketSet() override;

    UdpSocketSet(const UdpSocketSet&) = delete;
    UdpSocketSet& operator=(const UdpSocketSet&) = delete;

    /// Bind BindAddress::listenAddresses() on @p port (0 = one the OS picks, for all) and
    /// follow them in sync(). False when blocked or a bind failed.
    bool bind(uint16 port);
    /// Bind exactly @p addresses; sync() leaves them alone.
    bool bindTo(const QList<QHostAddress>& addresses, uint16 port);
    /// bind(): follow @p addresses (BindAddress::listenAddresses() of now). True = changed.
    bool sync(const QList<QHostAddress>& addresses);
    void close();

    /// At least one socket bound.
    [[nodiscard]] bool isBound() const;
    [[nodiscard]] uint16 localPort() const { return setPort(); }
    /// Why the last bind failed.
    [[nodiscard]] QString errorString() const { return m_error; }
    [[nodiscard]] int socketCount() const;
    [[nodiscard]] QList<qintptr> socketDescriptors() const;

    /// Owner thread.
    [[nodiscard]] bool hasPendingDatagrams() const;
    QNetworkDatagram receiveDatagram(qint64 maxSize = -1);

    /// Any thread. -1 when no socket can send to the destination (family not bound).
    qint64 writeDatagram(const QNetworkDatagram& datagram);
    qint64 writeDatagram(const QByteArray& data, const QHostAddress& host, quint16 port);
    qint64 writeDatagram(const char* data, qint64 len, const QHostAddress& host, quint16 port);
    /// The error of the last failed send.
    [[nodiscard]] QString sendErrorString() const;

    /// Set on every socket, now and bound later.
    void setSocketOption(QAbstractSocket::SocketOption option, const QVariant& value);
    /// Of the first socket; invalid while none is bound.
    [[nodiscard]] QVariant socketOption(QAbstractSocket::SocketOption option) const;

signals:
    /// A socket has datagrams: drain with receiveDatagram().
    void readyRead();

protected:
    BindResult bindOne(const QHostAddress& address, uint16 port, QString& error) override;
    void closeOne(const QHostAddress& address) override;
    [[nodiscard]] uint16 portOf(const QHostAddress& address) const override;
    [[nodiscard]] QString setName() const override { return m_name; }

private:
    struct Slot {
        QHostAddress address;
        std::unique_ptr<QUdpSocket> socket;
    };

    /// The socket for a datagram to @p dest; @p sender is honoured when one is bound to it
    /// (@p keepSender then says whether to keep it on the datagram). Caller holds m_lock.
    [[nodiscard]] QUdpSocket* pickLocked(const QHostAddress& dest, const QHostAddress& sender,
                                         bool& keepSender) const;

    QString m_name;
    std::vector<Slot> m_slots;
    /// m_slots: sends come from the throttler thread
    mutable std::mutex m_lock;
    QHash<int, QVariant> m_options;
    QString m_error;
    QString m_sendError;
    bool m_follow = false;
};

} // namespace eMule
