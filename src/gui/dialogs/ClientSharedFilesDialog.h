#pragma once

/// @file ClientSharedFilesDialog.h
/// @brief Dialog showing shared files received from a remote client.

#include <QCborArray>
#include <QDialog>

class QStandardItemModel;
class QTreeView;

namespace eMule {

class IpcClient;

/// Dialog that displays the shared file list received from a remote client
/// and allows the user to download selected files.
class ClientSharedFilesDialog : public QDialog {
    Q_OBJECT

public:
    explicit ClientSharedFilesDialog(const QString& clientName,
                                     const QCborArray& files,
                                     IpcClient* ipc,
                                     QWidget* parent = nullptr);

private:
    void downloadSelected();

    QTreeView* m_view = nullptr;
    QStandardItemModel* m_model = nullptr;
    IpcClient* m_ipc = nullptr;
};

} // namespace eMule
