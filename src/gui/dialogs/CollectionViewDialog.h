#pragma once

/// @file CollectionViewDialog.h
/// @brief Dialog for viewing .emulecollection files and downloading their contents.
///
/// Port of MFC CCollectionViewDialog (srchybrid/CollectionViewDialog.cpp).

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QTreeWidget;

namespace eMule {

class Collection;
class IpcClient;

class CollectionViewDialog : public QDialog {
    Q_OBJECT

public:
    /// Construct and populate from a loaded Collection.
    /// @param collection  The collection to display (ownership NOT transferred).
    /// @param ipc         IPC client for sending download requests.
    /// @param parent      Parent widget.
    explicit CollectionViewDialog(const Collection& collection,
                                  IpcClient* ipc = nullptr,
                                  QWidget* parent = nullptr);

private:
    void downloadSelected();
    void downloadAll();

    const Collection& m_collection;
    IpcClient* m_ipc = nullptr;
    QTreeWidget* m_tree = nullptr;
    QLineEdit* m_authorNameEdit = nullptr;
    QLineEdit* m_authorKeyHashEdit = nullptr;
    QCheckBox* m_addCategoryCheck = nullptr;
};

} // namespace eMule
