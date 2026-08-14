#pragma once

/// @file DetailDialog.h
/// @brief Shared chrome for the modeless detail dialogs — the Prev/Next walker
///        buttons of MFC's CListViewWalkerPropertySheet.
///
/// In the original client every detail sheet carries two small arrow buttons in
/// the bottom button row, immediately left of OK/Cancel
/// (srchybrid/ListViewWalkerPropertySheet.cpp:112-207). They step to the
/// previous/next row of the list the sheet was opened from and re-fill every
/// page. This base supplies that row and the plumbing; the walk itself is
/// supplied by the owning panel, which is the only thing that knows the view,
/// its proxy chain and how a row maps to an item key.
///
/// One divergence from MFC, on purpose: the buttons grey out at the first and
/// last row instead of staying enabled and beeping. The beep is kept as the
/// fallback for the race where the list changed since the last probe.

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCborMap>
#include <QDialog>
#include <QString>

#include <functional>

class QDialogButtonBox;
class QToolButton;
class QVBoxLayout;

namespace eMule {

class DetailDialog;
class IpcClient;

/// How a detail dialog walks the list it was opened from. Supplied by the panel.
///
/// @note `step` must advance its own anchor, and must do so **on the call**, not
/// when the daemon answers — otherwise a second click before the reply lands
/// would re-step from the same row. Neither callback may hold a QModelIndex:
/// the flat models reset on every poll tick and UiState::guardSelectionOnReset()
/// clears the view's current index with them, so the anchor has to be an item
/// key that is re-resolved against the live model on each step.
struct DetailWalker {
    /// Move the list's selection by @p delta (-1 up, +1 down) and return the key
    /// of the row it landed on. Empty at the ends of the list.
    std::function<QString(int delta)> step;

    /// Whether a step in @p delta would land somewhere, without moving anything.
    std::function<bool(int delta)> canStep;
};

/// Fetch the details for the key the walker landed on and hand them back.
/// Panels supply this because the request shape differs — one hash field for
/// downloads and shared files, `[searchID, hash]` for a search result.
using DetailRequestFactory = std::function<Ipc::IpcMessage(const QString& key)>;

/// Wire a dialog's walker to the daemon, so every panel that opens a detail
/// dialog shares one implementation of "step, fetch, swap in". A reply that a
/// later step has overtaken is discarded.
void connectDetailNavigation(DetailDialog* dialog, IpcClient* ipc,
                             DetailRequestFactory makeRequest);

/// Overload for the common single-hash request.
void connectDetailNavigation(DetailDialog* dialog, IpcClient* ipc,
                             Ipc::IpcMsgType detailsRequest);

/// Wire a dialog's "Search Kad" button to the daemon's SearchKadNotes request.
/// Kad notes arrive asynchronously over UDP, so this also re-fetches the details
/// twice on a timer — and drops both the timer and its reply if the walker has
/// moved the dialog onto another file meanwhile.
void connectKadNotesSearch(DetailDialog* dialog, IpcClient* ipc,
                           DetailRequestFactory makeRequest);

/// Overload for the common single-hash details request.
void connectKadNotesSearch(DetailDialog* dialog, IpcClient* ipc,
                           Ipc::IpcMsgType detailsRequest);

/// Wire the Comments page's "Edit spam filter..." to a single-key SetPreferences
/// push, keeping the GUI's own Preferences mirror in step. The daemon applies the
/// filter to incoming Kad notes, so nothing else has to be refreshed here.
void connectCommentFilter(DetailDialog* dialog, IpcClient* ipc);

/// Base for the detail dialogs: subclass content on top, and a bottom row with
/// the Prev/Next walker buttons immediately left of Close.
class DetailDialog : public QDialog {
    Q_OBJECT

public:
    explicit DetailDialog(QWidget* parent = nullptr);

    /// Rebuild the dialog from a fresh details map — content, window title and
    /// subject key. This is MFC's CListViewPropertySheet::ChangedData(): *every*
    /// page reflects the new item, not just the visible one.
    virtual void setDetails(const QCborMap& details) = 0;

    /// Refresh from a details map that describes the *same* item — a Kad notes
    /// lookup returning late. Subclasses that can repaint only the affected part
    /// override this to avoid tearing down tabs the user is scrolled into; the
    /// default is a full rebuild.
    virtual void applyDetails(const QCborMap& details) { setDetails(details); }

    /// Identity of the item currently on display (file hash / user hash). Late
    /// arrivals compare against it to avoid painting a file the user has already
    /// navigated away from.
    [[nodiscard]] QString subjectKey() const { return m_subjectKey; }

    /// Install the walk and show the arrow buttons. Without it the dialog gets
    /// only Close — MFC likewise omits the buttons when a sheet is opened
    /// without a list (ChatSelector, FriendListCtrl).
    void setWalker(DetailWalker walker);

signals:
    /// The walker moved to @p key; fetch its details and call setDetails().
    void navigated(const QString& key);

    /// The user pressed "Search Kad" for the item on display.
    void searchKadNotes(const QString& fileHash, const QString& fileName);

    /// The user edited the comment spam filter (a `|`-separated list); the owning
    /// panel pushes it to the daemon.
    void commentFilterChanged(const QString& filter);

protected:
    /// Where subclasses put their content. The button row sits below it.
    [[nodiscard]] QVBoxLayout* contentLayout() const { return m_contentLayout; }

    void setSubjectKey(const QString& key) { m_subjectKey = key; }

    bool event(QEvent* event) override;

private:
    void buildButtonRow();
    void walk(int delta);
    void refreshStepButtons();

    QVBoxLayout*      m_contentLayout = nullptr;
    QDialogButtonBox* m_buttonBox     = nullptr;
    QToolButton*      m_prevButton    = nullptr;
    QToolButton*      m_nextButton    = nullptr;
    DetailWalker      m_walker;
    QString           m_subjectKey;
};

} // namespace eMule
