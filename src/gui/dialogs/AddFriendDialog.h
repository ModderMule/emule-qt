#pragma once

/// @file AddFriendDialog.h
/// @brief "Add..." dialog for adding a friend, matching the MFC eMule layout.

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;
class QPushButton;

namespace eMule {

class AddFriendDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddFriendDialog(QWidget* parent = nullptr);

    /// Turn the dialog into the read-only sheet of an existing friend — MFC
    /// CAddFriend with m_pShowFriend set (srchybrid/AddFriend.cpp). @p lastSeen is
    /// seconds since the epoch, 0 for never.
    void showFriend(const QString& name, const QString& hash, const QString& address,
                    int port, const QString& kadId, qint64 lastSeen);

    [[nodiscard]] QString ipAddress() const;
    [[nodiscard]] int     port() const;
    [[nodiscard]] QString friendName() const;
    [[nodiscard]] QString friendHash() const;

private slots:
    void onAddClicked();

private:
    QLineEdit* m_ipEdit = nullptr;
    QLineEdit* m_portEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_hashEdit = nullptr;
    QLabel*    m_kadIdLabel = nullptr;
    QLabel*    m_lastSeenLabel = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
};

} // namespace eMule
