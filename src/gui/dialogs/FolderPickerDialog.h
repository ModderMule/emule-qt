#pragma once

/// @file FolderPickerDialog.h
/// @brief A folder on the core's machine. A local core gets the system's folder
/// dialog; one on another host this tree of its disk (IPC ListFolders).

#include "dialogs/RememberedSizeDialog.h"

class QLineEdit;
class QTreeView;

namespace eMule {

class FolderTreeModel;
class IpcClient;

class FolderPickerDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    FolderPickerDialog(IpcClient* ipc, const QString& title, const QString& startDir, QWidget* parent = nullptr);

    /// The picked folder, as typed or chosen ('/' separators)
    [[nodiscard]] QString selectedFolder() const;

    /// QFileDialog::getExistingDirectory() for the core's disk. Empty: cancelled.
    [[nodiscard]] static QString getExistingDirectory(QWidget* parent, IpcClient* ipc, const QString& title,
                                                      const QString& startDir);

private:
    FolderTreeModel* m_model;
    QTreeView* m_tree;
    QLineEdit* m_pathEdit;
};

} // namespace eMule
