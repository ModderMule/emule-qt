#pragma once

/// @file CoreScriptDialog.h
/// @brief Options › General › Core: save a script that starts eMule Qt against
///        this core from another computer (PowerShell, macOS .command or Linux .sh).

#include "dialogs/RememberedSizeDialog.h"

#include <QHostAddress>
#include <QList>
#include <QString>

#include <cstdint>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

namespace eMule {

class IpcClient;

class CoreScriptDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    /// @p localCore: the core runs here, so the address must be entered (public IP
    /// or domain). Else @p host is the remote core's, pre-filled and editable.
    /// @p listenAddress: ipc.listenAddress as known here, until the core tells.
    /// @p ipc: asks a local core where it listens and moves it (may be null).
    CoreScriptDialog(bool localCore, const QString& host, uint16_t port, const QString& token,
                     const QString& listenAddress, IpcClient* ipc, QWidget* parent = nullptr);

    [[nodiscard]] QString host() const;
    [[nodiscard]] uint16_t port() const;
    [[nodiscard]] bool embedToken() const;
    /// ipc.listenAddress the core took while open; empty = unchanged
    [[nodiscard]] QString changedListenAddress() const { return m_changedListen; }

    /// Address no other computer can dial: loopback, wildcard, localhost
    [[nodiscard]] static bool isLocalOnlyHost(const QString& host);
    /// Does a core listening on @p listening (ipc.listenAddress @p value) take
    /// connections for @p host? A name counts only when it is @p value itself.
    [[nodiscard]] static bool listensFor(const QList<QHostAddress>& listening, const QString& value,
                                         const QString& host);

private:
    void updateSaveButton();
    /// Save dialog, write the file; accept() on success
    void save();
    /// QR code for the qDonkey phone app (PhonePairDialog)
    void pairPhone();
    /// Ask the core where it listens
    void queryListen();
    /// Move the core's listener to @p value; offers "*" when it is not this machine's
    void listenOn(const QString& value);
    void setListenState(const QString& value, const QStringList& addresses);
    void updateListenRow();

    bool m_localCore = false;
    QString m_token;
    IpcClient* m_ipc = nullptr;
    QString m_listenValue;
    QList<QHostAddress> m_listening;
    QString m_changedListen;
    bool m_listenBusy = false;

    QLineEdit* m_hostEdit = nullptr;
    QSpinBox* m_portSpin = nullptr;
    QComboBox* m_targetCombo = nullptr;
    QCheckBox* m_embedTokenCheck = nullptr;
    QLabel* m_hostHint = nullptr;
    QWidget* m_listenRow = nullptr;
    QLabel* m_listenLabel = nullptr;
    QPushButton* m_listenBtn = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_phoneBtn = nullptr;
};

} // namespace eMule
