#pragma once

/// @file AddFriendDialog.h
/// @brief "Add..." dialog for adding a friend, matching the MFC eMule layout.

#include <QDialog>
#include <QString>

#include <functional>

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

    /// The address and port as entered, taken apart: the IP field may carry the port
    /// ("1.2.3.4:4662", "[2001:db8::1]:4662"), which then wins over the port field
    /// (MFC CAddFriend::OnAddBtn, AddFriend.cpp:112-139).
    [[nodiscard]] QString ipAddress() const;
    [[nodiscard]] int     port() const;
    [[nodiscard]] QString friendName() const;
    [[nodiscard]] QString friendHash() const;

    /// Split what the two fields hold into a literal address and a port.
    /// False when there is no valid address or no port from 1 to 65535.
    [[nodiscard]] static bool parseEndpoint(const QString& ipText, const QString& portText,
                                            QString& address, int& port);

    /// Who adds the friend. Called with the validated dialog; it reports back whether
    /// the friend was added. Without one, Add simply accepts.
    using Submitter = std::function<void(const AddFriendDialog&, std::function<void(bool)>)>;
    void setSubmitter(Submitter submitter) { m_submitter = std::move(submitter); }

    /// What an ed3k submitter reports: kEd3kAdded, kEd3kNoNode, else an ed3k error
    /// (ED3K_ERR_*, negative).
    static constexpr int kEd3kAdded = 1;
    static constexpr int kEd3kNoNode = 0;
    /// Who adds an `ed3k:friend` link (with the request text); the IP fields are then unused.
    using Ed3kSubmitter = std::function<void(const QString& link, const QString& text, std::function<void(int)>)>;
    void setEd3kSubmitter(Ed3kSubmitter submitter) { m_ed3kSubmitter = std::move(submitter); }
    /// Whether the ed3k node runs: the ed3k fields are enabled.
    void setEd3kAvailable(bool on);

    [[nodiscard]] QString ed3kLink() const;
    [[nodiscard]] QString ed3kText() const;

private slots:
    void onAddClicked();

private:
    void submitEd3k();
    [[nodiscard]] QString ed3kError(int code) const;

    QLineEdit* m_ipEdit = nullptr;
    QLineEdit* m_portEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_hashEdit = nullptr;
    QLabel*    m_kadIdLabel = nullptr;
    QLabel*    m_lastSeenLabel = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QLabel*    m_ipLabel = nullptr;
    Submitter  m_submitter;
    QWidget*   m_ed3kGroup = nullptr;
    QLineEdit* m_ed3kLinkEdit = nullptr;
    QLineEdit* m_ed3kTextEdit = nullptr;
    Ed3kSubmitter m_ed3kSubmitter;
};

} // namespace eMule
