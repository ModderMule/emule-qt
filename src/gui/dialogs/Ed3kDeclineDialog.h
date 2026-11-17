#pragma once

/// @file Ed3kDeclineDialog.h
/// @brief "Declined records" (ed3k Protocol.md §10.3, declined-records.md): edits the core's
/// decline.txt over IPC (GetEd3kDeclined 1208 / SetEd3kDeclined 1209) and shows what the
/// node loaded per list. No MFC original: a plain text dialog like the IP filter editor.
/// Below it the URLs of shared lists (pref ed3k.declineUrls, one per line) and how each
/// one's last fetch went.

#include <QDialog>
#include <QPointer>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QCborArray;
class QCborMap;

namespace eMule {

class IpcClient;

namespace Ipc {
class IpcMessage;
}

class Ed3kDeclineDialog : public QDialog {
    Q_OBJECT

public:
    explicit Ed3kDeclineDialog(IpcClient* ipc, QWidget* parent = nullptr);

    /// The GetEd3kDeclined / SetEd3kDeclined map: text (only when @p withText) and counts.
    void fill(const QCborMap& state, bool withText = true);
    /// The counts line of @p loaded (tests).
    [[nodiscard]] static QString countsText(const QCborMap& loaded);
    /// One line per URL of GetEd3kDeclined's urlStatus (tests).
    [[nodiscard]] static QString urlStatusText(const QCborArray& status);

private:
    void refresh();
    /// SetEd3kDeclined; @p close: accept() once saved.
    void save(bool close);

    QPointer<IpcClient> m_ipc;
    QPlainTextEdit* m_edit = nullptr;
    QLabel* m_countsLabel = nullptr;
    QLabel* m_invalidLabel = nullptr;
    QPlainTextEdit* m_urlsEdit = nullptr;
    QLabel* m_urlStatusLabel = nullptr;
    QPushButton* m_okBtn = nullptr;
    QPushButton* m_applyBtn = nullptr;
};

} // namespace eMule
