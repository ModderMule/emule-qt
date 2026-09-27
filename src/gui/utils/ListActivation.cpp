#include "pch.h"
/// @file ListActivation.cpp
/// @brief MFC list accelerators on Qt item views — see ListActivation.h.

#include "utils/ListActivation.h"

#include "dialogs/FindInListDialog.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPersistentModelIndex>
#include <QTreeView>
#include <QWidget>

#include <utility>

namespace eMule {

// ── helpers ────────────────────────────────────────────────────────────

namespace {

/// Everything that stops a keystroke from being a *plain* Enter. MFC's accelerator
/// entry names no modifier flag, which in an ACCELERATORS resource means it fires
/// only when none of Alt/Control/Shift is down. The keypad flag is not one of
/// those — the numeric-keypad Enter is still an Enter.
constexpr Qt::KeyboardModifiers kModifierMask =
    Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;

/// Watches one list for its accelerators. Deliberately has no Q_OBJECT: it
/// declares no signal or slot, and eventFilter() is a plain virtual, so nothing
/// here needs a meta-object.
class ListKeyFilter : public QObject {
public:
    ListKeyFilter(QAbstractItemView* view, ListKeyHandlers handlers)
        : QObject(view)
        , m_view(view)
        , m_h(std::move(handlers))
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::KeyPress)
            return QObject::eventFilter(watched, event);

        // An open cell editor owns the keyboard. Its Return commits the edit, and
        // QAbstractItemDelegate::eventFilter deliberately lets that key travel on —
        // QLineEdit then ignores it and Qt walks it up to the view, where it would
        // arrive here looking exactly like a keypress on the list itself. focusWidget()
        // is what tells the two apart: it names the editor while one is open and the
        // view once it has closed. Same for Del/⌫ and the clipboard keys.
        const QWidget* focus = m_view->focusWidget();
        if (focus && focus != m_view && m_view->isAncestorOf(focus))
            return QObject::eventFilter(watched, event);

        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            return handleEnter(key);
        if (handleCommand(key))
            return true;
        return QObject::eventFilter(watched, event);
    }

private:
    bool handleEnter(const QKeyEvent* key)
    {
        const Qt::KeyboardModifiers modifiers = key->modifiers();
        const bool alt = modifiers.testFlag(Qt::AltModifier);
        if (!alt && (modifiers & kModifierMask) != Qt::NoModifier)
            return false;

        // Both accelerators are swallowed whether or not this list acts on them, which
        // is what the original does: PreTranslateMessage returns TRUE for either
        // (srchybrid/MuleListCtrl.cpp:1508,1516) and a control with no case for the
        // command simply does nothing. Letting one through instead would hand Enter to
        // QAbstractItemView, which answers it by opening a cell editor.
        const QModelIndex             current = m_view->currentIndex();
        const ListActivationHandler& handler = alt ? m_h.details : m_h.activate;
        if (current.isValid() && handler)
            handler(current);
        return true;
    }

    /// The MPG_* / MP_* keys. True when the key was answered (and so swallowed);
    /// an unbound key travels on, so Qt's own handling (e.g. ⌫ keyboard search) stays.
    bool handleCommand(const QKeyEvent* key)
    {
        const bool plain = (key->modifiers() & kModifierMask) == Qt::NoModifier;
        auto run = [](const ListCommandHandler& handler) {
            if (!handler)
                return false;
            handler();
            return true;
        };

        if (plain) {
            switch (key->key()) {
            case Qt::Key_Delete:
            case Qt::Key_Backspace: return run(m_h.remove);
            case Qt::Key_F2:        return run(m_h.rename);
            case Qt::Key_F5:        return run(m_h.refresh);
            case Qt::Key_Insert:    return run(m_h.insert);
            default:                break;
            }
        }

        if (key->matches(QKeySequence::Copy))
            return run(m_h.copy);
        if (key->matches(QKeySequence::Paste))
            return run(m_h.paste);
        if (key->matches(QKeySequence::Cut))
            return run(m_h.cut);

        if (m_h.find) {
            const bool start = key->matches(QKeySequence::Find);
            const bool next = key->matches(QKeySequence::FindNext);
            const bool prev = key->matches(QKeySequence::FindPrevious);
            if (start || next || prev) {
                const QPersistentModelIndex before = m_view->currentIndex();
                if (start)
                    showFindInListDialog(m_view->window(), m_view);
                else
                    findNextInList(m_view->window(), m_view, prev);
                const QModelIndex after = m_view->currentIndex();
                if (m_h.found && after.isValid() && after != before)
                    m_h.found(after);
                return true;
            }
        }

        if (m_h.expandKeys && key->modifiers().testFlag(Qt::AltModifier)
            && (key->key() == Qt::Key_Right || key->key() == Qt::Key_Left)) {
            auto* tree = qobject_cast<QTreeView*>(m_view);
            QModelIndex current = m_view->currentIndex();
            if (!tree || !current.isValid())
                return false;
            current = current.siblingAtColumn(0);
            if (key->key() == Qt::Key_Right)
                tree->expand(current);
            else
                tree->collapse(current.parent().isValid() ? current.parent() : current);
            return true;
        }
        return false;
    }

    QAbstractItemView* m_view;
    ListKeyHandlers    m_h;
};

} // anonymous namespace

// ── public API ─────────────────────────────────────────────────────────

void bindListActivation(QAbstractItemView* view,
                        ListActivationHandler activate,
                        ListActivationHandler details)
{
    if (!view)
        return;

    // On the view itself, not its viewport: a list control has the keyboard focus
    // and handles key events in person, and an open cell editor is a separate
    // focus widget whose Return never reaches here — which is what keeps the
    // editable lists editable.
    ListKeyHandlers handlers;
    handlers.activate = std::move(activate);
    handlers.details = std::move(details);
    bindListKeys(view, std::move(handlers));
}

void bindListKeys(QAbstractItemView* view, ListKeyHandlers handlers)
{
    if (!view)
        return;
    view->installEventFilter(new ListKeyFilter(view, std::move(handlers)));
}

} // namespace eMule
