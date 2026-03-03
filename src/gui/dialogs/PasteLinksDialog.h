#pragma once

/// @file PasteLinksDialog.h
/// @brief Dialog for pasting eD2K links to download, matching MFC CDirectDownloadDlg.

#include <QDialog>

class QPlainTextEdit;
class QPushButton;

namespace eMule {

class IpcClient;

class PasteLinksDialog : public QDialog {
    Q_OBJECT

public:
    explicit PasteLinksDialog(IpcClient* ipc, QWidget* parent = nullptr);

private slots:
    void onDownload();

private:
    IpcClient* m_ipc;
    QPlainTextEdit* m_edit;
    QPushButton* m_downloadBtn;
};

} // namespace eMule
