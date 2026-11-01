#pragma once

/// @file FitTextTabBar.h
/// @brief Tab bar whose tabs always fit their full title; scroll arrows on overflow.
///
/// QMacStyle sizes a tab with an icon + close button narrower than its own
/// SE_TabBarTabText rect needs, so the title elides in a tab that looks wide.
/// tabSizeHint() adds the shortfall back. Eliding is off on purpose: with
/// ElideRight, Qt shrinks every tab to ~3 chars before it shows scroll arrows.
/// Tabs keep left: a fresh size hint after every relayout (macOS centers tabs
/// in a bar wider than they are, and a hint gone stale while hidden is wider).

#include <QTabBar>

namespace eMule {

class FitTextTabBar : public QTabBar {
public:
    explicit FitTextTabBar(QWidget* parent = nullptr);

    /// Narrowest tab, in px (0 = style default)
    void setMinimumTabWidth(int width);

protected:
    [[nodiscard]] QSize tabSizeHint(int index) const override;
    void tabLayoutChange() override;

private:
    int m_minTabWidth = 0;
};

} // namespace eMule
