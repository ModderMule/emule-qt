#pragma once

/// @file WebLinksDialog.h
/// @brief The links behind an eNode web search row.
///
/// The daemon fetched and verified the row's text/uri-list and parsed it; this
/// shows the links (file, name, size, hoster) and offers Download (Hoster tab,
/// while hoster downloads are on), Copy Links, Open in Browser and Save List.
/// Mirrors of one file share a file number. With hoster downloads on and
/// ed3kHosterCheckSearchLinks, the daemon checks each link as its download would
/// (CheckHosterLinks): Status (a colored dot) and Account (a gold star for premium) show it;
/// Download never waits for that.

#include "IpcMessage.h"


#include <QByteArray>
#include <QCborMap>
#include <QDialog>
#include <QString>
#include <QStringList>

class QCborArray;

class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class IpcClient;

class WebLinksDialog : public QDialog {
    Q_OBJECT

public:
    /// @p links: the daemon's map — title, protected, links [{url,file,name,size,hoster}].
    /// @p content: the uri-list as served, for Save List; @p fileName its suggested name.
    /// @p ipc: for the link checks; null: none.
    WebLinksDialog(const QString& rowName, const QCborMap& links, const QByteArray& content,
                   const QString& fileName, IpcClient* ipc, QWidget* parent = nullptr);

    /// The links of @p links [{url,file,...}] by file, in order: one download a file, the
    /// core goes on with its next mirror when one fails.
    [[nodiscard]] static QList<QStringList> mirrorsByFile(const QCborArray& links);

private:
    /// Whether any listed link is on a Tor hidden service.
    [[nodiscard]] bool hasOnionLink() const;
    /// The selected links, or every link when none is selected.
    [[nodiscard]] QStringList chosenUrls() const;
    /// The selected links, or every link, by file.
    [[nodiscard]] QList<QStringList> chosenDownloadFiles() const;
    void downloadLinks();
    /// Asks the daemon to check every link; the Status column stays hidden when it cannot.
    void startChecks();
    void onLinkChecked(const Ipc::IpcMessage& msg);
    void setStatus(QTreeWidgetItem* item, int status, int account);
    void copyLinks();
    void openLinks();
    void saveList();

    QTreeWidget* m_list = nullptr;
    QPushButton* m_openBtn = nullptr;
    QByteArray m_content;
    QString m_fileName;
    QString m_rowName;
    IpcClient* m_ipc = nullptr;
};

} // namespace eMule
