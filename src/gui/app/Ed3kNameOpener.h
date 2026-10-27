#pragma once

/// @file Ed3kNameOpener.h
/// @brief Opens an `ed3k:name` link (name.md §1.3): resolved by the core (ResolveEd3kName),
/// shown how it was resolved with its warnings and ties, asked before the first open
/// (§4.4 confirm, §5.3 DNS unverified), then opened (OpenEd3kName): a website in the
/// browser at its local origin, a torrent link through the existing path, a friend link
/// added as a friend. Also answers the website server's names typed in the browser that
/// need the user's yes (PushEd3kNameResolved with viaSite).

#include <QCborMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QWidget>

#include <functional>

namespace eMule {

namespace Ipc {
class IpcMessage;
}
class IpcClient;

class Ed3kNameOpener : public QObject {
    Q_OBJECT

public:
    /// Opens a link the name led to (not an ed3k:name one): ExternalLinkHandler::open.
    using LinkOpener = std::function<void(const QString& link)>;

    Ed3kNameOpener(IpcClient* ipc, LinkOpener openLink, QObject* parent = nullptr);

    /// The window the dialogs are parented to.
    void setDialogParent(QWidget* parent) { m_parent = parent; }
    /// Resolves @p text (an ed3k:name link) and opens it once the user agreed.
    void open(const QString& text);

    /// How a name was resolved, as the user sees it (§4.4): "yours", "pinned on …", …
    [[nodiscard]] static QString howText(const QCborMap& r);
    /// The first 8 characters of a zone's base32 (§4.4), from its hex.
    [[nodiscard]] static QString zoneText(const QString& hex);

private:
    void onResolved(const Ipc::IpcMessage& msg);
    void onFailed(const Ipc::IpcMessage& msg);
    /// [node, link]: offer to follow a friend's name list (non-modal).
    void onFriendList(const Ipc::IpcMessage& msg);
    /// Shows the resolution; true: the user wants it opened.
    [[nodiscard]] bool confirm(const QCborMap& r);
    void openResolved(qint64 id);

    QPointer<IpcClient> m_ipc;
    QPointer<QWidget> m_parent;
    LinkOpener m_openLink;
    /// ids of the resolutions this GUI asked for
    QSet<qint64> m_waiting;
    /// friend list links offered this run (asked once)
    QSet<QString> m_offered;
};

} // namespace eMule
