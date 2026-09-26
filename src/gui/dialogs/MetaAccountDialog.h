#pragma once

/// @file MetaAccountDialog.h
/// @brief Log in to an eNode server's Meta API account, or see its state.
///
/// Opens when a torrent/Usenet download answers AuthRequired or
/// AccountInactive, and from the server list's "eNode Account...". Accounts
/// are created on the server's website — the dialog only links there.
/// The daemon keeps the session token; the password is sent once, never stored.

#include <QCborMap>
#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;

namespace eMule {

class IpcClient;

class MetaAccountDialog : public QDialog {
    Q_OBJECT

public:
    enum class Purpose {
        Download,   ///< accept() as soon as the account can download — the caller retries
        Manage,     ///< account status + log out, from the server list
    };

    /// @p meta is a MetaStatus map (see IpcProtocol.h, 750-754).
    MetaAccountDialog(IpcClient* ipc, const QCborMap& meta, Purpose purpose, QWidget* parent = nullptr);

    [[nodiscard]] QString serverAddr() const { return m_serverAddr; }

    /// Newer status for the same server (another row hit the same wall).
    void updateMeta(const QCborMap& meta);

private:
    void applyMeta(const QCborMap& meta);
    void onLogin();
    void onLogout();
    void refresh();
    void setBusy(bool busy, const QString& note = {});
    [[nodiscard]] static QString link(const QString& url, const QString& text);

    IpcClient* m_ipc;
    Purpose m_purpose;
    QString m_serverAddr;
    QString m_serverName;

    QLabel* m_info = nullptr;
    QWidget* m_form = nullptr;
    QLineEdit* m_user = nullptr;
    QLineEdit* m_pass = nullptr;
    QLabel* m_steps = nullptr;
    QLabel* m_links = nullptr;
    QLabel* m_error = nullptr;
    QPushButton* m_loginButton = nullptr;
    QPushButton* m_logoutButton = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_closeButton = nullptr;
};

} // namespace eMule
