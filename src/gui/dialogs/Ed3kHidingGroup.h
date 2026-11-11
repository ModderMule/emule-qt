#pragma once

/// @file Ed3kHidingGroup.h
/// @brief The "Hiding" group of Options → ed3k → General (hops.md §1, §5, §6): the global
/// switch, one hop count for every use (0 to 3; 0 only while forwarding), forwarding for others and its
/// share, the proxy of anonymous share reports (subscription.md §5.3), "New anonymous key…",
/// and what hiding does and does not give (§6).

#include <QCborMap>
#include <QGroupBox>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace eMule {

class IpcClient;
namespace Ipc {
class IpcMessage;
}

class Ed3kHidingGroup : public QGroupBox {
    Q_OBJECT

public:
    Ed3kHidingGroup(IpcClient* ipc, QWidget* parent = nullptr);

    /// ed3kHide, ed3kHops, ed3kHopping, ed3kHopShare, ed3kReportProxy* of a preferences map.
    void load(const QCborMap& prefs);
    /// The same keys into a SetPreferences request.
    void appendTo(Ipc::IpcMessage& req) const;
    /// Without a core: into thePrefs.
    void saveLocal() const;
    /// GetEd3kStatus "hide" (empty: not running).
    void applyStatus(const QCborMap& hide);

    [[nodiscard]] bool hideChecked() const;
    [[nodiscard]] int hops() const;

    /// The §6 notes shown under the settings (also the switch's confirmation).
    [[nodiscard]] static QString notesText();

signals:
    void changed();
    /// The anonymous key changed: the page asks the status again.
    void keyChanged();

private:
    void newAnonKey();
    void updateEnabled();

    IpcClient* m_ipc = nullptr;
    QCheckBox* m_hideCheck = nullptr;
    QSpinBox* m_hopsSpin = nullptr;
    QCheckBox* m_hoppingCheck = nullptr;
    QLabel* m_zeroHint = nullptr;   ///< "0 = deniable, not hidden" (§2.6)
    QSpinBox* m_shareSpin = nullptr;
    QComboBox* m_reportProxyType = nullptr;   ///< data: PROXYTYPE_* (0 = none)
    QLineEdit* m_reportProxyHost = nullptr;
    QSpinBox* m_reportProxyPort = nullptr;
    QLineEdit* m_reportProxyUser = nullptr;
    QLineEdit* m_reportProxyPass = nullptr;
    QPushButton* m_newKeyBtn = nullptr;
    QLabel* m_statusLabel = nullptr;
    bool m_loading = false;
};

} // namespace eMule
