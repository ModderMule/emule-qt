#pragma once

/// @file FilterEdit.h
/// @brief List filter box — port of MFC CEditDelayed.
///
/// A line edit that filters one column of a list: the icon on the left picks the
/// column, the column's header shows as the hint while the box is empty, and the
/// text is reported 400 ms after the last key (at once on focus loss).

#include <QLineEdit>
#include <QPointer>

class QHeaderView;
class QTimer;

namespace eMule {

class FilterEdit : public QLineEdit {
    Q_OBJECT

public:
    explicit FilterEdit(QWidget* parent = nullptr);

    /// The header whose visible columns the column menu offers. The filter column
    /// falls back to 0 when its column is no longer there.
    void setHeader(QHeaderView* header);

    /// Columns the menu never offers (MFC CEditDelayed::OnInit's ignore list).
    void setIgnoredColumns(const QList<int>& columns) { m_ignored = columns; }

    [[nodiscard]] int filterColumn() const { return m_column; }

    /// Split a filter text into MFC's tokens: space separated, a lone "-" dropped.
    [[nodiscard]] static QStringList tokens(const QString& text);

    /// MFC CSearchListCtrl::IsFilteredOut: every token must be found in @p target,
    /// a "-token" must not; case-insensitive.
    [[nodiscard]] static bool matches(const QStringList& tokens, const QString& target);

signals:
    /// The filter to apply now.
    void filterChanged(const QStringList& tokens, int column);

protected:
    void focusOutEvent(QFocusEvent* event) override;

private:
    void showColumnMenu();
    void setColumn(int column);
    void evaluate();
    void updateHint();

    QPointer<QHeaderView> m_header;
    QTimer* m_timer = nullptr;
    int m_column = 0;
    QList<int> m_ignored;
    QStringList m_lastTokens;
    int m_lastColumn = 0;
};

} // namespace eMule
