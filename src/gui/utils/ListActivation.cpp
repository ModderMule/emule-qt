#include "pch.h"
/// @file ListActivation.cpp
/// @brief Enter / Alt+Enter on list views — see ListActivation.h.

#include "utils/ListActivation.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QKeyEvent>
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

/// Watches one list for the two accelerators. Deliberately has no Q_OBJECT: it
/// declares no signal or slot, and eventFilter() is a plain virtual, so nothing
/// here needs a meta-object.
class ActivationFilter : public QObject {
public:
    ActivationFilter(QAbstractItemView* view,
                     ListActivationHandler activate,
                     ListActivationHandler details)
        : QObject(view)
        , m_view(view)
        , m_activate(std::move(activate))
        , m_details(std::move(details))
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::KeyPress)
            return QObject::eventFilter(watched, event);

        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter)
            return QObject::eventFilter(watched, event);

        // An open cell editor owns the keyboard. Its Return commits the edit, and
        // QAbstractItemDelegate::eventFilter deliberately lets that key travel on —
        // QLineEdit then ignores it and Qt walks it up to the view, where it would
        // arrive here looking exactly like a keypress on the list itself. focusWidget()
        // is what tells the two apart: it names the editor while one is open and the
        // view once it has closed.
        const QWidget* focus = m_view->focusWidget();
        if (focus && focus != m_view && m_view->isAncestorOf(focus))
            return QObject::eventFilter(watched, event);

        const Qt::KeyboardModifiers modifiers = key->modifiers();
        const bool alt = modifiers.testFlag(Qt::AltModifier);
        if (!alt && (modifiers & kModifierMask) != Qt::NoModifier)
            return QObject::eventFilter(watched, event);

        // Both accelerators are swallowed whether or not this list acts on them, which
        // is what the original does: PreTranslateMessage returns TRUE for either
        // (srchybrid/MuleListCtrl.cpp:1508,1516) and a control with no case for the
        // command simply does nothing. Letting one through instead would hand Enter to
        // QAbstractItemView, which answers it by opening a cell editor.
        const QModelIndex             current = m_view->currentIndex();
        const ListActivationHandler& handler = alt ? m_details : m_activate;
        if (current.isValid() && handler)
            handler(current);
        return true;
    }

private:
    QAbstractItemView*    m_view;
    ListActivationHandler m_activate;
    ListActivationHandler m_details;
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
    view->installEventFilter(
        new ActivationFilter(view, std::move(activate), std::move(details)));
}

} // namespace eMule
