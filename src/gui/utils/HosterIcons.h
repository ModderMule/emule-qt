#pragma once

/// @file HosterIcons.h
/// @brief Favicons of hoster sites and the plugins that take an account, from the core
/// (GetHosterFavicons, GetHosterAccountHosts, PushHosterCatalog). One per IpcClient.
///
/// icon() answers at once: the site's favicon when the core has it, else a lettered stand-in
/// (as JDownloader draws one); the favicon is asked for once per run, iconChanged follows.

#include <QCborArray>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>

namespace eMule {

class IpcClient;
namespace Ipc {
class IpcMessage;
}

class HosterIcons : public QObject {
    Q_OBJECT

public:
    /// A plugin that takes an account.
    struct AccountHost {
        QString host;
        QString name;    ///< empty: its host
        bool multi = false;
        bool solver = false;
    };

    /// The cache of @p ipc (made on first use, a child of it); null without one.
    [[nodiscard]] static HosterIcons* of(IpcClient* ipc);

    /// The favicon of @p host, or a stand-in until it comes.
    [[nodiscard]] QIcon icon(const QString& host);

    /// The plugins that take an account, as last known; asks the core once per connection.
    [[nodiscard]] const QList<AccountHost>& accountHosts();
    /// Whether the core is still collecting them.
    [[nodiscard]] bool asking() const { return m_asking; }

    /// Two letters of @p host for its stand-in: the first and the middle consonant of the name
    /// without TLD (JD's FavIcons rule); the first two letters when it has none.
    [[nodiscard]] static QString abbreviation(const QString& host);

signals:
    void iconChanged(const QString& host);
    void accountHostsChanged();

private:
    explicit HosterIcons(IpcClient* ipc);
    void onPush(const Ipc::IpcMessage& msg);
    void applyIcons(const QHash<QString, QByteArray>& icons);
    void applyHosts(const QCborArray& hosts);
    void sendIconAsks();
    void reset();
    [[nodiscard]] static QIcon standIn(const QString& host);

    IpcClient* m_ipc;
    QHash<QString, QIcon> m_icons;      ///< favicons, and stand-ins of sites without one
    QSet<QString> m_asked;              ///< favicons asked for this connection
    QStringList m_toAsk;
    QTimer m_askTimer;                  ///< icons asked for in one request per paint
    QList<AccountHost> m_hosts;
    bool m_hostsAsked = false;
    bool m_asking = false;
};

} // namespace eMule
