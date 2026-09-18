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

class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLineEdit;
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

        /// Label for an optional password row under the box, empty for none.
        /// Only the NZB dialogs want it — a paste of ED2K links has nothing to
        /// decrypt — so it is a field rather than a second base class.
        QString passwordLabel;

        /// Show the box read-only, for a dialog that lists what the user already
        /// picked rather than asking them to type it. The accept button then
        /// stays enabled, since there is nothing for them to type into.
        bool readOnlyText = false;

        /// Offer a category / priority / start-paused row, and what to put in
        /// the category box: one entry per category in the user's own order,
        /// index 0 first. Empty for no row, which is every dialog but the two
        /// NZB ones — the same call Chrome::passwordLabel makes.
        QStringList queueCategories;
    };

protected:
    explicit PasteTextDialog(const Chrome& chrome, QWidget* parent = nullptr);

    /// Everything in the box, trimmed.
    [[nodiscard]] QString text() const;

    /// What was typed in the password row, or an empty string when the chrome
    /// asked for no row. Not trimmed: an archive passphrase is opaque bytes and
    /// a leading space is legal in one.
    [[nodiscard]] QString password() const;

    /// The category index picked, or 0 for "no category" — which is also what
    /// the daemon reads as "nobody chose", so auto-categorisation still runs.
    [[nodiscard]] int queueCategory() const;

    /// -2..+2, 0 when the chrome asked for no row.
    [[nodiscard]] int queuePriority() const;

    /// Whether to queue the release without starting it.
    [[nodiscard]] bool queuePaused() const;

    /// One entry per line, trimmed, blanks dropped.
    [[nodiscard]] QStringList lines() const;

    /// Replace the box's contents — used to leave only the entries that failed,
    /// so a retry does not re-send the ones that worked.
    void setLines(const QStringList& lines);

    /// Hold the accept button while requests are out. Both subclasses send over
    /// IPC and wait, and without this a second click queues everything twice.
    void setBusy(bool busy);

    /// A button left of Accept/Cancel, for an action that is not an answer — the
    /// place QDialogButtonBox gives an ActionRole button.
    QPushButton* addActionButton(const QString& text);

    /// Called when the accept button is pressed. The subclass decides whether
    /// and when to accept() — neither of these dialogs can close synchronously,
    /// because both are waiting on the daemon.
    virtual void onAccepted() = 0;

private:
    QPlainTextEdit* m_edit = nullptr;
    QLineEdit* m_password = nullptr;   ///< null unless Chrome::passwordLabel was set

    /// All null unless Chrome::queueCategories was non-empty.
    QComboBox* m_category = nullptr;
    QComboBox* m_priority = nullptr;
    QCheckBox* m_paused = nullptr;
    QPushButton* m_acceptBtn = nullptr;
    QHBoxLayout* m_buttonRow = nullptr;
    QString m_acceptText;
    bool m_readOnlyText = false;
};

} // namespace eMule
