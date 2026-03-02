#pragma once

/// @file NetworkInfoDialog.h
/// @brief Network Information dialog matching the MFC eMule Network Info popup.
///
/// Shows client info, eD2K network/server details, Kad network status,
/// and web interface status in a rich-text browser. Opens via double-click
/// on the status bar connection area.

#include <QDialog>

class QTextBrowser;

namespace eMule {

class IpcClient;

class NetworkInfoDialog : public QDialog {
    Q_OBJECT

public:
    explicit NetworkInfoDialog(IpcClient* ipc, QWidget* parent = nullptr);
    ~NetworkInfoDialog() override;

private:
    void requestNetworkInfo();
    void populateInfo(const QCborMap& info);
    static QString formatNumber(qint64 value);

    IpcClient* m_ipc = nullptr;
    QTextBrowser* m_browser = nullptr;
};

} // namespace eMule
