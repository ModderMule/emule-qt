#pragma once

/// @file NetworkInfoDialog.h
/// @brief Network Information dialog matching the MFC eMule Network Info popup.
///
/// Shows client info, eD2K network/server details, Kad network status, the ed3k
/// node, BitTorrent (external IPs, I2P) and web interface status in a rich-text browser. Opens via double-click
/// on the status bar connection area.

#include <QCborArray>
#include <QCborMap>
#include "dialogs/RememberedSizeDialog.h"

class QTextBrowser;

namespace eMule {

class IpcClient;

class NetworkInfoDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    /// @p anchor: section to scroll to once loaded (kEd3kAnchor), empty = top
    explicit NetworkInfoDialog(IpcClient* ipc, QWidget* parent = nullptr, const QString& anchor = {});
    ~NetworkInfoDialog() override;

    static constexpr auto kEd3kAnchor = "ed3k";

    /// The page for a GetNetworkInfo reply. @p extended: advanced controls, which
    /// add the server's feature list (MFC NetworkInfoDlg.cpp:182).
    [[nodiscard]] static QString infoHtml(const QCborMap& info, bool extended);
    /// GetNetworkInfo "ed3k" block; empty for a core without it. @p compact: lines for
    /// the Server window's My Info instead of tables
    [[nodiscard]] static QString ed3kHtml(const QCborMap& ed3k, bool compact = false);
    /// GetNetworkInfo "shared": per network with @p extended, else combined
    [[nodiscard]] static QString sharedHtml(const QCborMap& shared, bool extended, bool compact = false);

private:
    void requestNetworkInfo();
    void populateInfo(const QCborMap& info);
    static QString formatNumber(qint64 value);
    /// the index node dialled or held (§11): ed3k's eD2K server section
    static QString ed3kIndexHtml(const QCborArray& links, bool compact);
    /// GetNetworkInfo "bt" block; empty for a core without it
    static QString bitTorrentHtml(const QCborMap& bt);
    static QString i2pHtml(const QCborMap& bt);

    IpcClient* m_ipc = nullptr;
    QTextBrowser* m_browser = nullptr;
    QString m_anchor;
};

} // namespace eMule
