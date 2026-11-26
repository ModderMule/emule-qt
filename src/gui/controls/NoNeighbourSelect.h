#pragma once

/// @file NoNeighbourSelect.h
/// @brief View mixin: removing the current row selects nothing else.
///
/// A SingleSelection Qt view moves current to the next row when the current row
/// is removed, and selects it, even when the removed row wasn't selected. MFC
/// lists just DeleteItem (UploadListCtrl RemoveClient, KadContactListCtrl
/// ContactRem, FriendListCtrl RemoveFriend). Current still moves, so the keys work.

#include <QAbstractItemView>
#include <QItemSelectionModel>

namespace eMule {

template<class Base>
class NoNeighbourSelect : public Base {
public:
    using Base::Base;

protected:
    void rowsAboutToBeRemoved(const QModelIndex& parent, int start, int end) override
    {
        m_removingRows = true;
        Base::rowsAboutToBeRemoved(parent, start, end);
        m_removingRows = false;
    }

    QItemSelectionModel::SelectionFlags selectionCommand(const QModelIndex& index,
                                                         const QEvent* event = nullptr) const override
    {
        return m_removingRows ? QItemSelectionModel::NoUpdate : Base::selectionCommand(index, event);
    }

private:
    bool m_removingRows = false;
};

} // namespace eMule
