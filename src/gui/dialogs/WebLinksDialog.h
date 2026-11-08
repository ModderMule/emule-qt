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
/// A check box per link picks what Download, Copy Links and Open in Browser take, with
/// Select All / None / Invert above, as a torrent's or an NZB's Files tab. Download reads
/// "Download (n)" for n of several checked, "Download (all)" for all of them; the list's
/// context menu has Download and Check / Uncheck Selected.
/// Column widths, order and hidden columns are kept ("webLinks"); without a saved layout
/// Name is cut so the columns up to Status fit the window.

#include "IpcMessage.h"
#include "controls/AbstractListView.h"


#include <QByteArray>
#include <QCborMap>
#include "dialogs/RememberedSizeDialog.h"
#include <QString>
#include <QStringList>

class QCborArray;
class QPoint;

class QPushButton;
class QShowEvent;
class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class IpcClient;

class WebLinksDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    /// @p links: the daemon's map — title, protected, links [{url,file,name,size,hoster}].
    /// @p content: the uri-list as served, for Save List; @p fileName its suggested name.
    /// @p ipc: for the link checks; null: none.
    WebLinksDialog(const QString& rowName, const QCborMap& links, const QByteArray& content,
                   const QString& fileName, IpcClient* ipc, QWidget* parent = nullptr);
    ~WebLinksDialog() override;

    /// The links of @p links [{url,file,...}] by file, in order: one download a file, the
    /// core goes on with its next mirror when one fails.
    [[nodiscard]] static QList<QStringList> mirrorsByFile(const QCborArray& links);

protected:
    void showEvent(QShowEvent* event) override;

private:
    /// Whether any listed link is on a Tor hidden service.
    [[nodiscard]] bool hasOnionLink() const;
    /// The checked rows, in list order.
    [[nodiscard]] QList<QTreeWidgetItem*> checkedItems() const;
    /// The checked links.
    [[nodiscard]] QStringList chosenUrls() const;
    /// The checked links, by file.
    [[nodiscard]] QList<QStringList> chosenDownloadFiles() const;
    void downloadLinks();
    /// "Download (n)" with n of several checked, "Download (all)" with all, else "Download".
    void updateDownloadLabel();
    /// Download (the checked links) and Check / Uncheck Selected.
    void showContextMenu(const QPoint& pos);
    /// Asks the daemon to check every link; the Status column stays hidden when it cannot.
    void startChecks();
    void onLinkChecked(const Ipc::IpcMessage& msg);
    void setStatus(QTreeWidgetItem* item, int status, int account);
    void copyLinks();
    void openLinks();
    void saveList();
    /// Name as wide as its text, but File..Status stay in sight (fresh layout only).
    void fitNameColumn();
    /// Status/Account got data: back in the header menu, shown unless the user hid it.
    void unlockColumn(int column, bool wanted);
    /// Status/Account have no data (again): hidden and out of the header menu.
    void lockColumn(int column);

    ListTreeWidget* m_list = nullptr;
    QPushButton* m_openBtn = nullptr;
    QPushButton* m_downloadBtn = nullptr;   ///< null while hoster downloads are off
    QByteArray m_content;
    QString m_fileName;
    QString m_rowName;
    IpcClient* m_ipc = nullptr;
    bool m_freshLayout = false;   ///< no saved column layout: widths follow the text
    bool m_fitOnShow = false;     ///< fresh layout, Name not fitted yet
    bool m_wantStatus = true;     ///< the user's choice, kept while the column has no data
    bool m_wantAccount = true;
};

} // namespace eMule
