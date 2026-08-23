#pragma once

/// @file DialogSizing.h
/// @brief One place that decides how small a dialog is allowed to get.
///
/// Qt derives a window's minimum size from its layout only "unless a minimum size has
/// already been set" (QLayout::SetDefaultConstraint). Every dialog that hand-picks a
/// setMinimumSize()/setFixedSize() therefore *disables* that safety net: the layout is
/// forced to distribute the missing pixels and the form rows are painted clipped, which
/// is exactly what the Client Details dialog did on a short window.
///
/// The numbers in the dialogs were picked for one font, one language and one platform,
/// so they stay as a floor only — the content raises them whenever it needs more.

#include <QSize>

class QLayout;
class QWidget;

namespace eMule::DialogSizing {

/// How much room the content is entitled to.
enum class Fit {
    /// Nothing may be squeezed — a dialog of labels and form rows. Their layout minimum
    /// is one text line per row, so honouring it is what produced the half-height rows
    /// in the first place; the *preferred* height is the readable one.
    Content,

    /// The content gives way — lists, text edits, the Options pages. Their preferred
    /// height is a suggestion, so inflating the window to it would only add empty space;
    /// the layout minimum is the honest floor here (and the one Qt would apply itself,
    /// were the dialog not overriding it).
    Layout,
};

/// Give @p dialog a minimum size that shows all of its content, and a default size at
/// least that large. Both are clamped to what the screen can actually display.
///
/// @param designedMin      the hand-picked floor the call site used to pass to
///                         setMinimumSize(); pass 0 for a dimension the content owns.
/// @param designedDefault  the hand-picked default from resize(); grown to the minimum.
/// @param fit              which height the content is entitled to; see Fit.
///
/// Call at the *end* of the constructor, once the content widgets exist. Safe to call
/// again after the content was rebuilt: a dialog the user has already enlarged is only
/// ever grown, never snapped back to its default.
void applySize(QWidget* dialog, QSize designedMin, QSize designedDefault = {},
               Fit fit = Fit::Content);

/// Non-resizable variant for the dialogs that match a fixed MFC layout: the size becomes
/// max(@p designed, what the content needs), clamped to the screen. Always Fit::Content —
/// a window the user cannot resize has to show everything on its own.
void applyFixedSize(QWidget* dialog, QSize designed);

/// How tall @p layout has to be at @p width for nothing inside it to be clipped —
/// totalSizeHint() plus whatever wrapping adds at that width. Exposed because a scroll
/// area has to measure its own content by the same rule.
int neededHeight(QLayout* layout, int width);

/// Let @p widget's height follow its width through the layout chain.
///
/// QWidgetItem::hasHeightForWidth() reads the *size policy* flag, not whether the widget
/// implements heightForWidth(). So every widget between a word-wrapping label and the
/// window has to opt in — miss one and the layout budgets the label's single-line hint
/// while the wrapped lines are painted outside the row, i.e. invisibly.
void enableHeightForWidth(QWidget* widget);

} // namespace eMule::DialogSizing
