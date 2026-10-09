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
class QPushButton;
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
    /// Saving writes <incoming>/<name>.emulecollection like MFC, so there is no
    /// source file to update in place; renaming saves a new one.
    void loadExistingCollection(const QString& name,
                                const QList<QVariantMap>& files,
                                bool textFormat);

private:
    void setupUi();
    void populateSharedFiles();
    /// One row of either list: name, size, hash (MFC CCollectionListCtrl's columns).
    [[nodiscard]] static QTreeWidgetItem* makeRow(const QString& name, qint64 size, const QString& hash);
    void addToCollection();
    void removeFromCollection();
    void updateLabels();
    void onSave();
    void onFormatChanged();

    /// @p overwrite: the user has agreed to replace an existing file of that name.
    void sendSave(bool overwrite);

    IpcClient* m_ipc;
    QTreeWidget* m_sharedTree = nullptr;      // left pane
    QTreeWidget* m_collectionTree = nullptr;  // right pane
    /// Titles the left list and switches it between the shared and the known files
    /// (MFC IDC_COLLECTIONVIEWSHAREBUTTON).
    QPushButton* m_sharedButton = nullptr;
    bool m_showKnown = false;
    quint32 m_fillSerial = 0;   ///< drops the answer of a fetch the toggle has overtaken
    QLabel* m_collectionLabel = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QCheckBox* m_textFormatCheck = nullptr;
    QCheckBox* m_signCheck = nullptr;

    QStringList m_preselectedHashes;
};

} // namespace eMule
