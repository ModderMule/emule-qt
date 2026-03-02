#pragma once

/// @file AddFriendDialog.h
/// @brief "Add..." dialog for adding a friend, matching the MFC eMule layout.

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;

namespace eMule {

class AddFriendDialog : public QDialog {
    Q_OBJECT

public:
    explicit AddFriendDialog(QWidget* parent = nullptr);

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
};

} // namespace eMule
