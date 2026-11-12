#pragma once

/// @file BtI2pPage.h
/// @brief Options > BitTorrent > I2P (issue #118).
///
/// Router: off, the bundled i2pd or an external router's SAM bridge. Mixed mode, new
/// torrents I2P only, the bundled router's browser proxy, ed3k joins of I2P torrents, tunnels and the I2P tracker list.
/// The router's state comes from GetBtStatus / PushBtStatus. No MFC original.

#include <QCborMap>
#include <QStringList>
#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace eMule {

class IpcClient;

namespace Ipc {
class IpcMessage;
}

class BtI2pPage : public QWidget {
    Q_OBJECT

public:
    BtI2pPage(IpcClient* ipc, QWidget* parent);

    /// From the GetPreferences map (btI2p*, ed3kEnabled). Signals stay quiet meanwhile.
    void load(const QCborMap& prefs);
    /// Appends the btI2p* key/value pairs to a SetPreferences request.
    void appendTo(Ipc::IpcMessage& req) const;

signals:
    void changed();

private:
    void updateEnabled();
    void editTrackers();
    void requestStatus();
    void applyStatus(const QCborMap& status);

    IpcClient* m_ipc = nullptr;
    QButtonGroup* m_modeGroup = nullptr;
    QLineEdit* m_hostEdit = nullptr;
    QSpinBox* m_portSpin = nullptr;
    QSpinBox* m_routerPortSpin = nullptr;
    QSpinBox* m_lengthSpin = nullptr;
    QSpinBox* m_quantitySpin = nullptr;
    QCheckBox* m_mixedCheck = nullptr;
    QCheckBox* m_onlyNewCheck = nullptr;
    QCheckBox* m_ed3kJoinCheck = nullptr;
    QCheckBox* m_browserProxyCheck = nullptr;
    QSpinBox* m_browserProxyPortSpin = nullptr;
    QCheckBox* m_statusPageCheck = nullptr;
    QSpinBox* m_statusPagePortSpin = nullptr;
    QPushButton* m_statusPageBtn = nullptr;
    QLineEdit* m_statusPageUserEdit = nullptr;
    QLineEdit* m_statusPagePassEdit = nullptr;
    QPushButton* m_trackersBtn = nullptr;
    QLabel* m_statusLabel = nullptr;
    QStringList m_trackers;
    bool m_bundledAvailable = true;
    bool m_ed3kOn = true;
};

} // namespace eMule
