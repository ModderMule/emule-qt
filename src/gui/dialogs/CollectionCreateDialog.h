#pragma once

/// @file CollectionCreateDialog.h
/// @brief Dialog for creating and modifying .emulecollection files.
///
/// Port of MFC CCollectionCreateDialog (srchybrid/CollectionCreateDialog.cpp).
/// Dual-pane layout: available shared files (left) and collection files (right).

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class IpcClient;

class CollectionCreateDialog : public QDialog {
    Q_OBJECT

public:
    /// Create mode: selectedHashes are pre-added to the right pane.
    explicit CollectionCreateDialog(IpcClient* ipc,
                                     const QStringList& selectedHashes = {},
                                     QWidget* parent = nullptr);

    /// Load existing collection data for modify mode.
    void loadExistingCollection(const QString& collectionHash,
                                const QString& name,
                                const QList<QVariantMap>& files,
                                bool textFormat);

private:
    void setupUi();
    void populateSharedFiles();
    void addToCollection();
    void removeFromCollection();
    void updateLabels();
    void onSave();
    void onFormatChanged();

    IpcClient* m_ipc;
    QTreeWidget* m_sharedTree = nullptr;      // left pane
    QTreeWidget* m_collectionTree = nullptr;  // right pane
    QLabel* m_sharedLabel = nullptr;
    QLabel* m_collectionLabel = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QCheckBox* m_textFormatCheck = nullptr;
    QCheckBox* m_signCheck = nullptr;

    QStringList m_preselectedHashes;
    QString m_existingCollectionHash;  // non-empty in modify mode
};

} // namespace eMule
