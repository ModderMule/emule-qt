#pragma once

/// @file Ed3kSetsDialog.h
/// @brief "ed3k Sets": the followed ed3k sets (file-set.md §5.3) and the live streams watched
/// (live.md), over IPC 1110-1119 (pushes 1155-1157). No MFC original: laid out like eMule's
/// list dialogs (a list on top, the member tree with check boxes below, buttons at the right).
///
/// - Sets: name, author (short key), version (a newer one waiting: "1 → 2"), files, size,
///   policy, state. Follow link…, Unfollow, the policy (ask / download), "Download selected"
///   (the members ticked; takes a pending version), "Download parts" (open parts).
/// - Live: watched streams with their state; Watch link…, Open (the local stream URL in the
///   system's player or browser; local core only), Stop.
/// - One dialog (showDialog), non-modal; also from ExternalLinkHandler (ed3k:set, ed3k:live).

#include <QCborArray>
#include <QCborMap>
#include <QDialog>
#include <QPointer>
#include <QString>

class QComboBox;
class QLabel;
class QPushButton;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class IpcClient;

class Ed3kSetsDialog : public QDialog {
    Q_OBJECT

public:
    enum Tab { TabSets = 0, TabLive = 1 };

    /// The one dialog, shown and raised on @p tab.
    static Ed3kSetsDialog* showDialog(QWidget* parent, IpcClient* ipc, Tab tab = TabSets);

    Ed3kSetsDialog(IpcClient* ipc, QWidget* parent = nullptr);
    ~Ed3kSetsDialog() override;

    /// The GetEd3kSets / GetEd3kLive maps (pushes and refreshes; tests).
    void fillSets(const QCborMap& sets);
    void fillLive(const QCborMap& live);
    /// Selects the set @p id in the list.
    void selectSet(qint64 id);

    [[nodiscard]] static QString policyText(int policy);
    [[nodiscard]] static QString memberStateText(int state);
    [[nodiscard]] static QString liveStateText(int state);

private:
    void refresh();
    void onSetChanged();
    void fillMembers(const QCborMap& set);
    void updateButtons();
    [[nodiscard]] QCborMap currentSet() const;
    [[nodiscard]] qint64 currentId() const;
    [[nodiscard]] QString currentStream() const;
    void followLink();
    void unfollow();
    void setPolicy(int index);
    void downloadSelected();
    void downloadParts();
    void watchLink();
    void openStream();
    void leaveStream();
    /// Leaf paths whose box is not ticked.
    [[nodiscard]] QStringList skippedPaths() const;
    void showError(const QString& what, const QCborMap& reply);

    QPointer<IpcClient> m_ipc;
    QTabWidget* m_tabs = nullptr;
    QTreeWidget* m_sets = nullptr;
    QTreeWidget* m_members = nullptr;
    QComboBox* m_policy = nullptr;
    QLabel* m_info = nullptr;
    QPushButton* m_follow = nullptr;
    QPushButton* m_unfollow = nullptr;
    QPushButton* m_download = nullptr;
    QPushButton* m_parts = nullptr;
    QTreeWidget* m_live = nullptr;
    QPushButton* m_watch = nullptr;
    QPushButton* m_open = nullptr;
    QPushButton* m_leave = nullptr;
    QCborArray m_setRows;
    /// what the member tree shows: set id, version, pending version (rebuilt when they move)
    qint64 m_shownId = -1;
    qint64 m_shownSeq = -2;
    qint64 m_shownPending = -2;
    bool m_filling = false;
};

} // namespace eMule
