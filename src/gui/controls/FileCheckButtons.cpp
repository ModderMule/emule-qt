#include "pch.h"
/// @file FileCheckButtons.cpp
/// @brief Select All / Select None / Invert Selection over a list of check boxes.

#include "controls/FileCheckButtons.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

namespace eMule::FileCheckButtons {

QWidget* create(QWidget* parent, std::function<void(Bulk)> apply, QList<QPushButton*>* buttons)
{
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);

    const auto add = [&](const char* text, Bulk mode) {
        auto* button = new QPushButton(QCoreApplication::translate("FileCheckButtons", text), row);
        QObject::connect(button, &QPushButton::clicked, row, [apply, mode] { apply(mode); });
        layout->addWidget(button);
        if (buttons)
            buttons->append(button);
    };
    add(QT_TRANSLATE_NOOP("FileCheckButtons", "Select All"), Bulk::All);
    add(QT_TRANSLATE_NOOP("FileCheckButtons", "Select None"), Bulk::None);
    add(QT_TRANSLATE_NOOP("FileCheckButtons", "Invert Selection"), Bulk::Invert);
    layout->addStretch(1);
    return row;
}

void applyToLeaves(QTreeWidget* tree, int column, Bulk mode)
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        QTreeWidgetItem* item = *it;
        if (item->childCount() > 0 || !(item->flags() & Qt::ItemIsUserCheckable))
            continue;
        const bool checked = item->checkState(column) == Qt::Checked;
        const bool want = mode == Bulk::All ? true : mode == Bulk::None ? false : !checked;
        if (want != checked)
            item->setCheckState(column, want ? Qt::Checked : Qt::Unchecked);
    }
}

} // namespace eMule::FileCheckButtons
