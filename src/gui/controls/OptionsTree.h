#pragma once

/// @file OptionsTree.h
/// @brief MFC-style settings tree (the Extended page of eMule's options): group rows with an
/// icon, number rows shown as "Label: value" with an edit mark, edited in place (double-click,
/// F2), and check rows.

#include <QTreeWidget>

namespace eMule {

class OptionsTree : public QTreeWidget {
    Q_OBJECT

public:
    explicit OptionsTree(QWidget* parent = nullptr);

    /// A top-level group, expanded.
    QTreeWidgetItem* addGroup(const QString& text, const QIcon& icon);
    /// A number row under @p group. @p special: shown for @p min (as QSpinBox::specialValueText).
    QTreeWidgetItem* addSpin(QTreeWidgetItem* group, const QString& label, int min, int max,
                             const QString& special = {}, const QString& suffix = {});
    /// A check row under @p group.
    QTreeWidgetItem* addCheck(QTreeWidgetItem* group, const QString& label);

    [[nodiscard]] int value(const QTreeWidgetItem* item) const;
    /// Clamped to the row's range; emits nothing.
    void setValue(QTreeWidgetItem* item, int value);
    [[nodiscard]] bool isChecked(const QTreeWidgetItem* item) const;
    /// Emits nothing.
    void setChecked(QTreeWidgetItem* item, bool on);
    /// The text a number row shows for @p value.
    [[nodiscard]] static QString valueText(const QTreeWidgetItem* item, int value);

    enum Role { KindRole = Qt::UserRole + 1, LabelRole, MinRole, MaxRole, SpecialRole, SuffixRole, ValueRole };
    enum Kind { Group = 0, Spin = 1, Check = 2 };

signals:
    /// The user edited a row.
    void changed();

private:
    void showValue(QTreeWidgetItem* item);
    /// The "I" mark of a number row (MFC's edit icon).
    [[nodiscard]] QIcon editIcon() const;

    bool m_quiet = false;
};

} // namespace eMule
