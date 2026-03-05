#pragma once

/// @file PasteLinksDialog.h
/// @brief Dialog for pasting eD2K links to download, matching MFC CDirectDownloadDlg.

#include <QDialog>

class QPlainTextEdit;
class QPushButton;

namespace eMule {

class DownloadListModel;
class IpcClient;
class SharedFilesModel;

class PasteLinksDialog : public QDialog {
    Q_OBJECT

public:
    explicit PasteLinksDialog(IpcClient* ipc,
                              DownloadListModel* dlModel = nullptr,
                              SharedFilesModel* sfModel = nullptr,
                              QWidget* parent = nullptr);

private slots:
    void onDownload();

private:
    IpcClient* m_ipc;
    DownloadListModel* m_dlModel;
    SharedFilesModel* m_sfModel;
    QPlainTextEdit* m_edit;
    QPushButton* m_downloadBtn;
};

} // namespace eMule
