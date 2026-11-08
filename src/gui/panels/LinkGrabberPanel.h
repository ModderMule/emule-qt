#pragma once

/// @file LinkGrabberPanel.h
/// @brief The Link Grabber tab of the Hoster tab (hoster.md "Link Grabber"), as in
/// JDownloader: crawled links in packages, checked online, edited before they download.
///
/// The list is the core's (Ed3kLinkGrabber, IPC 1210-1218, push 1190); this panel shows it
/// and sends the edits. A package row holds its links; a link's check box enables it.

#include "controls/AbstractListView.h"

#include <QCborMap>
#include <QMap>
#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidgetItem;

namespace eMule {

class IpcClient;

namespace Ipc {
class IpcMessage;
}

class LinkGrabberPanel : public QWidget {
    Q_OBJECT

public:
    explicit LinkGrabberPanel(QWidget* parent = nullptr);
    ~LinkGrabberPanel() override;

    void setIpcClient(IpcClient* ipc);
    /// The resolver crawls pages: the Add Links dialog offers "Deep crawl".
    void setDeepCrawl(bool on) { m_deep = on; }
    /// Asks for links (text, package, passwords, deep crawl); they are crawled into the list.
    void promptAddLinks(const QStringList& prefill = {});
    /// Crawls the http(s) links of @p text into the list. @p clipboard: only links a plugin takes.
    void grab(const QString& text, const QString& source, bool deep = false, const QString& package = {},
              const QStringList& passwords = {}, bool clipboard = false);
    [[nodiscard]] int linkCount() const { return int(m_links.size()); }

signals:
    /// The number of links or the crawling count changed (the tab's label).
    void countChanged(int links, int crawling);

private:
    void refresh();
    void apply(const QCborMap& m, bool all);
    void upsertPackage(const QCborMap& p);
    void upsertLink(const QCborMap& k);
    void removeLink(qint64 id);
    void removePackage(qint64 id);
    void updatePackage(qint64 id);
    void fillLink(QTreeWidgetItem* row, const QCborMap& k);
    void setCrawling(int n);
    void showContextMenu(const QPoint& pos);
    void onItemChanged(QTreeWidgetItem* row, int column);
    /// Links of the selection: selected links and every link of a selected package.
    [[nodiscard]] QList<qint64> selectedLinkIds() const;
    /// Packages of the selection: selected package rows only.
    [[nodiscard]] QList<qint64> selectedPackageIds() const;
    /// Packages a selected row belongs to.
    [[nodiscard]] QList<qint64> touchedPackageIds() const;
    void confirm(const QList<qint64>& packageIds, const QList<qint64>& linkIds);
    void renamePackage();
    void setPasswords();
    void mergeSelection();
    void removeSelection();
    void cleanup(const QString& mode);
    void copyLinks();
    void openInBrowser();
    /// Sends @p req; a refusal is shown.
    void send(Ipc::IpcMessage req);

    QPointer<IpcClient> m_ipc;
    ListTreeWidget* m_tree = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    bool m_deep = false;
    bool m_filling = false;   ///< rows being set: their check boxes are no edit
    int m_crawling = 0;
    QMap<qint64, QCborMap> m_packages;
    QMap<qint64, QCborMap> m_links;
    QMap<qint64, QTreeWidgetItem*> m_packageRows;
    QMap<qint64, QTreeWidgetItem*> m_linkRows;
};

} // namespace eMule
