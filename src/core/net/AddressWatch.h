#pragma once

/// @file AddressWatch.h
/// @brief Tells when a local interface address comes or goes, as the OS reports it.
///
/// Qt has no such signal: QNetworkInformation reports reachability and the transport
/// medium only (Linux: only through NetworkManager), QNetworkInterface can only be polled.
/// Sources: a netlink socket (Linux), a routing socket (macOS/BSD),
/// NotifyUnicastIpAddressChange (Windows). A burst of events (VPN up, DAD done) folds into
/// one changed(). The 5 s watchdog in CoreSession stays as the fallback.

#include <QObject>
#include <QTimer>

#include <memory>

class QSocketNotifier;

namespace eMule {

class AddressWatch : public QObject {
    Q_OBJECT

public:
    explicit AddressWatch(QObject* parent = nullptr);
    ~AddressWatch() override;

    AddressWatch(const AddressWatch&) = delete;
    AddressWatch& operator=(const AddressWatch&) = delete;

    /// False when the OS source could not be opened: only the watchdog polls.
    [[nodiscard]] bool isActive() const { return m_active; }

    /// One OS event (any thread); folded into changed(). Tests call it too.
    void noteEvent();

signals:
    /// An address or link changed; the settle delay after the last event has passed.
    void changed();

private:
    void openSource();
    void closeSource();
    void drain();

    static constexpr int kSettleMs = 250;

    QTimer m_settle;
    bool m_active = false;
#if defined(Q_OS_WIN)
    void* m_handle = nullptr;           ///< NotifyUnicastIpAddressChange handle
#else
    int m_fd = -1;                      ///< netlink / routing socket
    std::unique_ptr<QSocketNotifier> m_notifier;
#endif
};

} // namespace eMule
