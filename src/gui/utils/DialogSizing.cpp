#include "pch.h"
/// @file DialogSizing.cpp
/// @brief Content-driven dialog minimum sizes — see DialogSizing.h.

#include "utils/DialogSizing.h"

#include "controls/ContentScrollArea.h"

#include <QBoxLayout>
#include <QGuiApplication>
#include <QLayout>
#include <QScreen>
#include <QTabBar>
#include <QTabWidget>
#include <QWidget>

#include <algorithm>

namespace eMule::DialogSizing {

// ── helpers ────────────────────────────────────────────────────────────

namespace {

/// How large a window may grow before it stops fitting on the screen it opens on.
/// A minimum taller than this would push the button row under the dock or off the
/// bottom edge, which is worse than a scrollbar.
QSize screenBudget(const QWidget* dialog)
{
    const QScreen* screen = dialog->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return {QWIDGETSIZE_MAX, QWIDGETSIZE_MAX};

    const QRect avail = screen->availableGeometry();
    return {std::max(320, avail.width() - 40), std::max(240, avail.height() - 60)};
}

/// The size at which nothing in @p dialog is clipped.
///
/// Width comes from the layout *minimum* — the size hint of a wrapping label is as wide
/// as its text, so a long file name would otherwise blow the dialog across the screen.
///
/// The height depends on what the content is (see Fit). A form row's layout minimum is a
/// single line of text, so a dialog of labels has to claim its preferred height, measured
/// at the width it will really have.
QSize contentSize(QWidget* dialog, QSize designedMin, Fit fit)
{
    QLayout* layout = dialog->layout();
    if (!layout)
        return designedMin;

    // Invalidate first, and the whole tree of them: activate() is happy to hand back
    // hints cached before the content was swapped, and invalidating the dialog's own
    // layout does not reach the nested ones. That stale data is how a re-fit ends up
    // sizing the window for the item the user just navigated away from.
    for (QLayout* nested : dialog->findChildren<QLayout*>())
        nested->invalidate();
    layout->invalidate();
    layout->activate();

    const int width  = std::max(designedMin.width(), layout->totalMinimumSize().width());
    int       height = layout->totalMinimumSize().height();

    if (fit == Fit::Content)
        height = std::max(height, neededHeight(layout, width));

    return {width, std::max(designedMin.height(), height)};
}

/// Widest tab row a dialog was sized for so far.
constexpr char kWidestTabTitles[] = "_emule_widestTabTitles";

/// The dialog width at which no tab title of @p dialog is elided, 0 without tabs.
///
/// A tab bar without scroll buttons (the macOS style) squeezes its tabs into whatever
/// width it gets, and its minimum hint is the squeezed one — so the layout never asks
/// for the room the titles need.
int tabTitlesWidth(const QWidget* dialog)
{
    int width = 0;
    for (const QTabWidget* tabs : dialog->findChildren<QTabWidget*>()) {
        if (tabs->tabBar()->usesScrollButtons())
            continue;

        int needed = tabs->tabBar()->sizeHint().width();
        for (const QWidget* w = tabs; w && w != dialog; w = w->parentWidget()) {
            const QWidget* parent = w->parentWidget();
            if (parent && parent->layout()) {
                const QMargins m = parent->layout()->contentsMargins();
                needed += m.left() + m.right();
            }
        }
        width = std::max(width, needed);
    }
    return width;
}

} // anonymous namespace

// ── public API ─────────────────────────────────────────────────────────

/// How tall @p layout has to be at @p width for nothing inside it to be clipped.
///
/// totalSizeHint() is the base: it counts the margins and spacings correctly, but asks
/// every item how tall it would like to be at no particular width, so anything that wraps
/// is counted a line or more short. This adds what each item really needs at the width it
/// is going to get. Doing it this way round rather than through totalHeightForWidth() is
/// deliberate — that one answers with its own idea of the spacings, dropping them.
int neededHeight(QLayout* layout, int width)
{
    int height = layout->totalSizeHint().height();

    const QMargins margins = layout->contentsMargins();
    const int      inner   = width - margins.left() - margins.right();

    for (int i = 0; i < layout->count(); ++i) {
        const QLayoutItem* item   = layout->itemAt(i);
        const QWidget*     widget = item->widget();
        int                needed = -1;

        if (const auto* area = qobject_cast<const ContentScrollArea*>(widget)) {
            // Has to be asked by name: QWidgetItem answers heightForWidth() from the
            // scroll area's own viewport layout, so the form inside is invisible to
            // every generic measurement.
            needed = area->contentHeightForWidth(inner);
        } else if (widget && qobject_cast<QVBoxLayout*>(widget->layout())) {
            // A plain stacked container — its children each get the full width, so the
            // same rule applies one level down. Everything else (a form, a group box, a
            // tab widget) is left to Qt: those implement heightForWidth themselves.
            needed = neededHeight(widget->layout(), inner);
        } else if (item->hasHeightForWidth()) {
            needed = item->heightForWidth(inner);
        }

        if (needed > 0)
            height += std::max(0, needed - item->sizeHint().height());
    }

    return height;
}

void applySize(QWidget* dialog, QSize designedMin, QSize designedDefault, Fit fit)
{
    if (!dialog)
        return;

    const QSize budget = screenBudget(dialog);
    QSize       needed;

    // Measure, apply, measure again. A word-wrapping label answers sizeHint() differently
    // once it has been laid out at the narrower width it ends up with, so the first
    // measurement can be a line short — and the line it is short of is the one the user
    // would not be able to read. It settles after a pass or two; the count is the stop.
    for (int pass = 0; pass < 4; ++pass) {
        const QSize measured = contentSize(dialog, designedMin, fit).boundedTo(budget);
        if (measured == needed)
            break;

        needed = measured;
        dialog->setMinimumSize(needed);

        // Only the first call places the window. Once it is up the user's own size wins —
        // a detail dialog re-fits itself after every walker step, and snapping back to the
        // default there would undo their resize on every arrow click.
        // The default is bounded like the minimum: a hand-picked 700 px is taller than a
        // scaled laptop screen, and a window whose bottom edge is under the taskbar cannot
        // be dragged smaller.
        // The default alone is widened for the tab titles: they stay a wish, so the
        // user can still drag the window narrower.
        // A window that is up follows only when the tab row got wider than it ever was
        // (a walker step onto a file with one more page) — not on every re-fit, which
        // would undo a narrower size the user chose.
        const int titles = tabTitlesWidth(dialog);
        const int widest = dialog->property(kWidestTabTitles).toInt();
        dialog->setProperty(kWidestTabTitles, std::max(titles, widest));

        const QSize wished(std::max(designedDefault.width(), titles), designedDefault.height());
        const QSize grown(titles > widest ? std::max(dialog->width(), titles) : dialog->width(),
                          dialog->height());
        dialog->resize(dialog->isVisible()
                           ? grown.expandedTo(needed).boundedTo(budget.expandedTo(dialog->size()))
                           : wished.expandedTo(needed).boundedTo(budget));
    }
}

void applyFixedSize(QWidget* dialog, QSize designed)
{
    if (!dialog)
        return;

    const QSize budget = screenBudget(dialog);
    QSize       needed;

    for (int pass = 0; pass < 3; ++pass) {   // settles like applySize(), see there
        const QSize measured = contentSize(dialog, designed, Fit::Content).boundedTo(budget);
        if (measured == needed)
            break;
        needed = measured;
        dialog->setFixedSize(needed);
    }
}

void enableHeightForWidth(QWidget* widget)
{
    if (!widget)
        return;

    QSizePolicy policy = widget->sizePolicy();
    policy.setHeightForWidth(true);
    widget->setSizePolicy(policy);
}

} // namespace eMule::DialogSizing
