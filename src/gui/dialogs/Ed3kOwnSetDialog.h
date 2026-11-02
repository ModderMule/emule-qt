#pragma once

/// @file Ed3kOwnSetDialog.h
/// @brief "Create ed3k Set…" (My ed3k Sets, batch 15, IPC 1146): laid out like MFC's Create
/// Collection dialog (docs/eMuleScreensGUI/"Shared Files Collection.png"): the shared files
/// left, the set's files right, the name below; ed3k options: the kind (open, hidden: the link
/// is a key, paid: sold with Terms… through SellEd3k kind 2). A folder set lists no files: every
/// file below the folder (at each new version too).

#include "dialogs/RememberedSizeDialog.h"

#include <QCborMap>
#include <QStringList>

#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class IpcClient;

class Ed3kOwnSetDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    /// IPC kinds (GetEd3kOwnSets)
    enum Kind { Open = 1, Hidden = 2, Paid = 3 };

    /// @p files: core paths that start in the set; @p folder: a folder set (no file lists).
    Ed3kOwnSetDialog(IpcClient* ipc, const QStringList& files, const QString& folder, QWidget* parent = nullptr);

    /// Opens it (deleted on close); Save publishes, a paid set is sold next.
    static void run(IpcClient* ipc, QWidget* parent, const QStringList& files, const QString& folder = {});

    [[nodiscard]] QString title() const;
    [[nodiscard]] int kind() const;
    /// The set's files (core paths); empty for a folder set.
    [[nodiscard]] QStringList files() const;

private:
    void setupUi();
    void populateSharedFiles();
    [[nodiscard]] static QTreeWidgetItem* makeRow(const QString& name, qint64 size, const QString& path);
    void addToSet();
    void removeFromSet();
    void updateLabels();
    void onKindChanged();
    /// the terms of a paid set (Ed3kSellTermsDialog); false: cancelled
    bool askTerms();
    void onSave();
    /// SellEd3k kind 2 of the set @p id just made
    void sellNew(const QString& id);

    IpcClient* m_ipc;
    QString m_folder;
    QStringList m_preselected;
    QTreeWidget* m_sharedTree = nullptr;
    QTreeWidget* m_setTree = nullptr;
    QLabel* m_sharedLabel = nullptr;
    QLabel* m_setLabel = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_kind = nullptr;
    QPushButton* m_termsButton = nullptr;
    QLabel* m_kindNote = nullptr;
    QPushButton* m_saveButton = nullptr;
    std::optional<QCborMap> m_terms;
    QCborMap m_defaults;   ///< GetEd3kSelling defaults of a new offer
};

} // namespace eMule
