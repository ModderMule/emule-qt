#pragma once

/// @file Ed3kFilterPage.h
/// @brief Options → ed3k → Filters (filter-list.md): followed filter lists, their mode, and
/// the user's own verdicts. Acts at once over IPC (1130-1135, push 1170), not on OK.
///
/// §3: only lists the user chose; the default is to mark, hiding and refusing are set per
/// list; nothing is deleted or stopped because of a list.

#include <QCborMap>
#include <QPointer>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QTreeWidget;

namespace eMule {

class IpcClient;

namespace Ipc {
class IpcMessage;
}

class Ed3kFilterPage : public QWidget {
    Q_OBJECT

public:
    Ed3kFilterPage(IpcClient* ipc, QWidget* parent);
    ~Ed3kFilterPage() override;

    /// The GetEd3kFilters map (tests and refreshes).
    void fill(const QCborMap& filters);

    [[nodiscard]] static QString verdictText(int verdict);
    [[nodiscard]] static QString subjectText(int subject);
    /// mode bits: 1 hide, 2 refuse
    [[nodiscard]] static QString modeText(int mode);

private:
    void refresh();
    void follow();
    void unfollow();
    /// A list file opened by hand (§2): no author, no updates.
    void openFile();
    void applyMode(QComboBox* combo, qint64 id);
    void addOwn();
    void removeOwn();
    /// Shows a failed reply; true when it failed.
    bool failed(const Ipc::IpcMessage& reply, const QString& what);
    [[nodiscard]] static int modeOfIndex(int index);
    [[nodiscard]] static int indexOfMode(int mode);

    QPointer<IpcClient> m_ipc;
    QLabel* m_note = nullptr;
    QTreeWidget* m_lists = nullptr;
    QComboBox* m_listMode = nullptr;
    QComboBox* m_ownMode = nullptr;
    QTreeWidget* m_own = nullptr;
    bool m_filling = false;
};

} // namespace eMule
