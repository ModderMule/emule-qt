#pragma once

/// @file ToolbarCustomizeDialog.h
/// @brief Dual-list dialog for reordering/adding/removing toolbar buttons.

#include "app/MainWindow.h"

#include <QDialog>
#include <QList>

class QListWidget;
class QPushButton;

namespace eMule {

class ToolbarCustomizeDialog : public QDialog {
    Q_OBJECT

public:
    explicit ToolbarCustomizeDialog(const QList<ToolbarButtonId>& currentOrder,
                                    QWidget* parent = nullptr);

    [[nodiscard]] QList<ToolbarButtonId> order() const { return m_order; }

signals:
    void orderChanged(const QList<ToolbarButtonId>& newOrder);

private:
    void populateLists();
    void onAdd();
    void onRemove();
    void onMoveUp();
    void onMoveDown();
    void onReset();

    QListWidget* m_availableList = nullptr;
    QListWidget* m_currentList = nullptr;
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;
    QPushButton* m_moveUpBtn = nullptr;
    QPushButton* m_moveDownBtn = nullptr;

    QList<ToolbarButtonId> m_order;
    QList<ToolbarButtonId> m_initialOrder;
};

} // namespace eMule
