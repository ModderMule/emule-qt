#pragma once

/// @file CoreAttachment.h
/// @brief Which core this GUI talks to, and whether closing the GUI stops it.
///
/// A local core is attached by default: it stops when the GUI closes, also one
/// the GUI found running, e.g. started by the updater (#106, #120). The user can
/// detach it: the core then keeps running. A remote core never stops with the GUI.

#include <QIcon>
#include <QObject>
#include <QString>

#include <cstdint>

namespace eMule {

class CoreAttachment : public QObject {
    Q_OBJECT

public:
    static CoreAttachment& instance();

    /// Where the core is. @p remote = another machine.
    void setTarget(const QString& host, uint16_t port, bool remote);
    /// This GUI started the core (shown in the location text)
    void noteLaunched();

    [[nodiscard]] bool isRemote() const { return m_remote; }
    [[nodiscard]] bool launchedByGui() const { return m_launched; }
    [[nodiscard]] bool stopsOnExit() const { return !m_remote && !m_userDetached; }
    /// Only a local core can be attached; a remote one never stops with the GUI.
    [[nodiscard]] bool canToggle() const { return !m_remote; }
    /// "127.0.0.1:4712"; brackets around an IPv6 host
    [[nodiscard]] QString address() const;

    /// User choice: true = attach (stop core on exit), false = detach.
    void setStopsOnExit(bool stop);
    void toggle() { setStopsOnExit(!stopsOnExit()); }

    /// "Local core at …" / "Remote core at …"
    [[nodiscard]] QString locationText() const;
    /// What happens to the core when the GUI closes
    [[nodiscard]] QString exitText() const;
    /// Both lines, for a tooltip
    [[nodiscard]] QString toolTip() const;
    /// Chain link: closed = attached, broken = detached
    [[nodiscard]] QIcon icon(const QColor& color) const;

signals:
    void changed();

private:
    CoreAttachment() = default;

    QString m_host;
    uint16_t m_port = 0;
    bool m_remote = false;
    bool m_launched = false;
    bool m_userDetached = false;   ///< attached by default (#120); kept across relaunch/reconnect
};

} // namespace eMule
