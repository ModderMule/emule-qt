#pragma once

/// @file PasteTextDialog.h
/// @brief The shape both paste dialogs share: a box, one entry per line, Download.
///
/// PasteLinksDialog and AddNzbUrlDialog differ only in what a line *means* and
/// where it is sent. Everything else — the label, the multi-line box, the
/// Download/Cancel row, enabling Download only when there is something to send,
/// the remembered size — was identical, so it lives here once.
///
/// The chrome arrives through the constructor rather than through virtual
/// getters on purpose: the base builds its UI in its own constructor, and a
/// virtual call from there still dispatches to the base.

#include <QDialog>
#include <QSize>
#include <QString>
#include <QStringList>

class QPlainTextEdit;
class QPushButton;

namespace eMule {

class PasteTextDialog : public QDialog {
    Q_OBJECT

public:
    /// Everything the subclasses differ in visually.
    struct Chrome {
        QString title;
        QString iconPath;
        QString label;
        QString placeholder;
        QString acceptText;
        QSize   defaultSize{450, 250};
    };

protected:
    explicit PasteTextDialog(const Chrome& chrome, QWidget* parent = nullptr);

    /// Everything in the box, trimmed.
    [[nodiscard]] QString text() const;

    /// One entry per line, trimmed, blanks dropped.
    [[nodiscard]] QStringList lines() const;

    /// Replace the box's contents — used to leave only the entries that failed,
    /// so a retry does not re-send the ones that worked.
    void setLines(const QStringList& lines);

    /// Hold the accept button while requests are out. Both subclasses send over
    /// IPC and wait, and without this a second click queues everything twice.
    void setBusy(bool busy);

    /// Called when the accept button is pressed. The subclass decides whether
    /// and when to accept() — neither of these dialogs can close synchronously,
    /// because both are waiting on the daemon.
    virtual void onAccepted() = 0;

private:
    QPlainTextEdit* m_edit = nullptr;
    QPushButton* m_acceptBtn = nullptr;
    QString m_acceptText;
};

} // namespace eMule
