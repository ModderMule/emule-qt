#pragma once

/// @file HostnameResolver.h
/// @brief Reverse DNS of the peer addresses a list shows ("Resolve downloaders' hostnames").

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace eMule {

/// PTR lookups for the addresses of a polled list, a few at a time. Results, failures too,
/// stay while their address is listed, so a poll never queries the same peer again.
class HostnameResolver : public QObject {
    Q_OBJECT

public:
    explicit HostnameResolver(QObject* parent = nullptr);
    ~HostnameResolver() override;

    /// A literal unicast address; not a name, "any", multicast or broadcast.
    [[nodiscard]] static bool isResolvable(const QString& address);

    [[nodiscard]] bool enabled() const { return m_enabled; }
    /// Off aborts the lookups in flight; finished results are kept for turning it on again.
    void setEnabled(bool on);

    /// The addresses now listed: drop the results of the others and queue
    /// @p candidates not looked up yet.
    void sync(const QSet<QString>& listed, const QStringList& candidates);
    /// Every listed address is a candidate.
    void sync(const QStringList& addresses);

    /// Looked up (found or not) or in flight.
    [[nodiscard]] bool isKnown(const QString& address) const
    {
        return m_hostnames.contains(address) || m_lookups.contains(address);
    }
    [[nodiscard]] bool isPending(const QString& address) const { return m_lookups.contains(address); }
    /// The name found; empty when off, pending or without a PTR record.
    [[nodiscard]] QString hostname(const QString& address) const;
    /// The name found, else @p fallback.
    [[nodiscard]] QString displayText(const QString& address, const QString& fallback) const;
    /// "name:port" once resolved, else "ip:port" ("[v6]:port"); @p port 0: no port.
    [[nodiscard]] QString endpointText(const QString& address, uint16_t port) const;
    /// "name\nIP", "IP\nNo hostname found.", "IP (resolving…)", or the IP.
    [[nodiscard]] QString toolTip(const QString& address) const { return toolTip(address, 0); }
    /// toolTip() showing "ip:port" for the IP; @p port 0: no port.
    [[nodiscard]] QString toolTip(const QString& address, uint16_t port) const;

signals:
    /// A lookup of @p address finished; its rows need a repaint.
    void resolved(const QString& address);

private:
    void startLookups();

    bool m_enabled = false;
    quint64 m_generation = 0;
    QHash<QString, QString> m_hostnames;   ///< empty value: no PTR record
    QHash<QString, int> m_lookups;         ///< in flight, QHostInfo ids
    QQueue<QString> m_queue;
};

} // namespace eMule
