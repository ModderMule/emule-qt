#pragma once

/// @file Ed3kOptionsPages.h
/// @brief The "ed3k" group of the Options dialog: General, Connection, Credits, Names.
///
/// Kept out of OptionsDialog.cpp. The pages own their widgets; OptionsDialog adds them to
/// its stack, fills them from the daemon's preference map and collects them on OK/Apply.
/// No MFC original: laid out like the Usenet and BitTorrent pages.
/// - Credits (credit.md): off by default; switching them on shows the §9/§9.1/§10 notes and
///   asks; the key is made, imported or exported over local IPC only (1020-1022).
/// - Names (name.md): pins, petnames and followed name lists act at once over IPC
///   (1012-1019), not on OK.

#include <QCborMap>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QWidget;

namespace eMule {

class IpcClient;
struct Ed3kCreditPrefs;

namespace Ipc {
class IpcMessage;
}

class Ed3kOptionsPages : public QObject {
    Q_OBJECT

public:
    /// Pages are parented to @p parent (the dialog).
    Ed3kOptionsPages(IpcClient* ipc, QWidget* parent);
    ~Ed3kOptionsPages() override;

    [[nodiscard]] QWidget* generalPage() const { return m_general; }
    [[nodiscard]] QWidget* connectionPage() const { return m_connection; }
    [[nodiscard]] QWidget* creditsPage() const { return m_credits; }
    [[nodiscard]] QWidget* namesPage() const { return m_names; }

    /// From the GetPreferences map (keys ed3k*, nick). Signals stay quiet meanwhile.
    void load(const QCborMap& prefs);
    /// Appends key/value pairs of every ed3k preference to a SetPreferences request.
    void appendTo(Ipc::IpcMessage& req) const;
    /// Without a daemon: into thePrefs.
    void saveLocal() const;
    /// The contract address and chain ID are well-formed; else shows why and returns false.
    [[nodiscard]] bool validate();

signals:
    /// A setting was edited (OptionsDialog::markDirty).
    void changed();
    /// validate() failed on this page: OptionsDialog shows it.
    void showPage(QWidget* page);

private:
    QWidget* buildGeneral(QWidget* parent);
    QWidget* buildConnection(QWidget* parent);
    QWidget* buildCredits(QWidget* parent);
    QWidget* buildNames(QWidget* parent);
    /// GetEd3kStatus / PushEd3kStatus: running note, credit address, balance, stake.
    void applyStatus(const QCborMap& status);
    void requestStatus();
    /// The user ticked "Use credits": the notes of credit.md §9, §9.1, §10; false: not agreed.
    [[nodiscard]] bool confirmCredits();
    void newKey();
    void importKey();
    void exportKey();
    void refreshNames();
    void fillNames(const QCborMap& names);
    void addPetname();
    void followList();
    void moveList(int step);
    /// Shows a failed names/credit reply; true when it failed.
    bool failed(const Ipc::IpcMessage& reply, const QString& what);
    [[nodiscard]] QStringList extOff() const;
    [[nodiscard]] Ed3kCreditPrefs creditPrefs() const;
    [[nodiscard]] bool keyActionsAllowed() const;
    [[nodiscard]] static QString notesText();
    [[nodiscard]] static QString creditsText(qint64 micro);

    QPointer<IpcClient> m_ipc;
    QWidget* m_parent = nullptr;
    bool m_loading = false;

    QWidget* m_general = nullptr;
    QWidget* m_connection = nullptr;
    QWidget* m_credits = nullptr;
    QWidget* m_names = nullptr;

    // General
    QLabel* m_runningLabel = nullptr;
    QCheckBox* m_enabledCheck = nullptr;
    QLabel* m_nickLabel = nullptr;
    QCheckBox* m_shareFilesCheck = nullptr;
    QCheckBox* m_holdBackCheck = nullptr;
    QList<QCheckBox*> m_extChecks;   ///< text = the extension name

    // Connection
    QSpinBox* m_udpPortSpin = nullptr;
    QCheckBox* m_lanCheck = nullptr;
    QSpinBox* m_sitePortSpin = nullptr;
    QPlainTextEdit* m_nodeListsEdit = nullptr;
    QPlainTextEdit* m_chaintubeRpcEdit = nullptr;

    // Credits
    QCheckBox* m_creditEnabledCheck = nullptr;
    QGroupBox* m_ledgerGroup = nullptr;
    QLineEdit* m_chainIdEdit = nullptr;
    QLineEdit* m_contractEdit = nullptr;
    QPlainTextEdit* m_creditRpcEdit = nullptr;
    QSpinBox* m_quorumSpin = nullptr;
    QDoubleSpinBox* m_bidSpin = nullptr;
    QCheckBox* m_sellCheck = nullptr;
    QDoubleSpinBox* m_sellFloorSpin = nullptr;
    QSpinBox* m_sellSlotsSpin = nullptr;
    QLabel* m_addressLabel = nullptr;
    QPushButton* m_newKeyBtn = nullptr;
    QPushButton* m_importKeyBtn = nullptr;
    QPushButton* m_exportKeyBtn = nullptr;
    QLabel* m_accountLabel = nullptr;

    // Names
    QLabel* m_namesNote = nullptr;
    QTreeWidget* m_pinsTree = nullptr;
    QTreeWidget* m_petnamesTree = nullptr;
    QTreeWidget* m_listsTree = nullptr;
};

} // namespace eMule
