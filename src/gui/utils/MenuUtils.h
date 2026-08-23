#pragma once

/// @file MenuUtils.h
/// @brief Small helpers shared by the panels' context menus.

class QAction;
class QMenu;

namespace eMule {

/// Mark @p action as @p menu's default entry — the port of MFC's
/// CMenu::SetDefaultItem, which draws that entry in bold to show what a double
/// click (and Enter) would do.
///
/// QMenu::setDefaultAction() alone is not enough: the macOS style ignores the
/// QStyleOptionMenuItem::DefaultItem flag, so the entry renders like any other
/// and the hint is lost. The bold therefore goes on the action's own font as
/// well; setDefaultAction() is still called, because that is what carries the
/// meaning on the styles which do honour it.
///
/// Passing a null @p action clears the menu's default entry, which is MFC's
/// SetDefaultItem(-1) — used wherever the primary action does not apply to the
/// current selection.
void setMenuDefaultAction(QMenu* menu, QAction* action);

} // namespace eMule
