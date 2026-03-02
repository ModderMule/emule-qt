#pragma once

/// @file TransferToolbar.h
/// @brief Section header toolbar widget for the Transfer panel.
///
/// Replicates the MFC m_btnWnd1 / m_btnWnd2 header bars that show a bold
/// label with a row of toggled icon buttons for view switching.

#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QToolButton;

namespace eMule {

/// A thin header toolbar with an icon, bold text label, and a row of
/// exclusive toggle buttons — matching the MFC Transfer window section headers.
class TransferToolbar : public QWidget {
    Q_OBJECT

public:
    explicit TransferToolbar(QWidget* parent = nullptr);

    /// Add a toggle button with the given icon and tooltip. Returns the button ID.
    int addButton(const QIcon& icon, const QString& tooltip);

    /// Programmatically check a button by ID.
    void checkButton(int id);

    /// Update the bold text label (e.g. "Downloads (5)").
    void setLabelText(const QString& text);

    /// Set the leading icon shown before the label (e.g. current view icon).
    void setLeadingIcon(const QIcon& icon);

signals:
    /// Emitted when a toolbar button is toggled on.
    void buttonClicked(int id);

private:
    QHBoxLayout* m_layout = nullptr;
    QLabel* m_iconLabel = nullptr;
    QLabel* m_label = nullptr;
    QButtonGroup* m_group = nullptr;
    int m_nextId = 0;
};

} // namespace eMule
