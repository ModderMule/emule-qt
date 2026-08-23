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
#include <QSize>
#include <QString>

#include <functional>

class QDialogButtonBox;
class QFormLayout;
class QLabel;
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

/// Fetch GetClientDetails for @p clientHash and show a ClientDetailDialog for it,
/// parented to @p parent. Shared because more than one panel opens that dialog from
/// nothing but a user hash, and the exchange is the same every time.
///
/// @param walker  omitted where the dialog was not opened from a list, which drops
///                the Prev/Next arrows — MFC likewise omits them for ChatSelector
///                and FriendListCtrl.
///
/// Nothing happens when the daemon does not know the client: an offline friend has
/// no entry in the client list, and the original answers that case with its
/// CAddFriend sheet rather than a detail dialog.
void showClientDetails(QWidget* parent, IpcClient* ipc, const QString& clientHash,
                       DetailWalker walker = {});

/// Wire the Comments page's "Edit spam filter..." to a single-key SetPreferences
/// push, keeping the GUI's own Preferences mirror in step. The daemon applies the
/// filter to incoming Kad notes, so nothing else has to be refreshed here.
void connectCommentFilter(DetailDialog* dialog, IpcClient* ipc);

/// A value label for a detail form row: selectable, and wrapping across the full width
/// of the row rather than at whatever width QLabel's own heuristic picks — a value that
/// wrapped inside a 90 pixel column while the dialog was 700 wide is how the File Details
/// size and source counts used to lose their second and third lines.
[[nodiscard]] QLabel* detailValueLabel(const QString& text);

/// Add a bold "@p label:" / @p value row to @p form, the way every detail sheet does.
void addDetailRow(QFormLayout* form, const QString& label, const QString& value);

/// Base for the detail dialogs: subclass content on top, and a bottom row with
/// the Prev/Next walker buttons immediately left of Close.
class DetailDialog : public QDialog {
    Q_OBJECT

public:
    /// Whether the content sits in a scroll area. Only the plain form dialogs need it:
    /// they are sized to show everything, so it scrolls solely on a screen too short for
    /// the form. A tabbed dialog shrinks its pages instead and must not scroll its tab
    /// bar away.
    enum class ContentScroll { Off, On };

    explicit DetailDialog(QWidget* parent = nullptr,
                          ContentScroll scroll = ContentScroll::Off);

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

    /// The hand-picked size floors, in place of setMinimumSize()/resize(): the content
    /// raises them whenever it needs more room. Pass 0 for a dimension the content owns.
    void setDesignedSize(QSize minimum, QSize preferred);

    /// Re-measure the content and grow the dialog to fit it. The walker rebuilds every
    /// page on each step, so each setDetails() override ends with this.
    void fitToContent();

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
    QSize             m_designedMin;
    QSize             m_designedDefault;
};

} // namespace eMule
