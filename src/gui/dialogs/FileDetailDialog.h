#pragma once

/// @file FileDetailDialog.h
/// @brief Tabbed file-detail dialog (General / File Names / Comments / Media Info
///        / Metadata / ED2K Link / Archive Preview).

#include "CommentsPanel.h"
#include "DetailDialog.h"

#include <QCborMap>

class QCheckBox;
class QLabel;
class QTabWidget;
class QTextEdit;
class QTreeWidget;

namespace eMule {

class IpcClient;
class FileDetailDialog;

/// Wire a dialog's requestEd2kLink() to the daemon's GetEd2kLink request, so every
/// panel that opens the dialog shares one implementation of that exchange.
/// A stale reply (the user toggled another box meanwhile) is discarded.
void connectEd2kLinkRequests(FileDetailDialog* dialog, IpcClient* ipc);

/// Tabbed property dialog for a download file, matching the original MFC
/// FileDetailDialog. Shows General info, Source File Names, Comments,
/// Media Info, Metadata (ED2K tags), ED2K Link, and Archive Preview.
class FileDetailDialog : public DetailDialog {
    Q_OBJECT

public:
    /// Tab indices matching the original MFC property sheet order.
    enum Tab {
        General = 0, FileNames = 1, Comments = 2,
        MediaInfo = 3, Metadata = 4, Ed2kLink = 5, ArchivePreview = 6
    };

    /// Construct from a CBOR details map (as returned by GetDownloadDetails).
    /// @param details  Full CBOR map with extended download details.
    /// @param initialTab  Which tab to show initially.
    explicit FileDetailDialog(const QCborMap& details,
                              Tab initialTab = General,
                              QWidget* parent = nullptr);

    /// Swap the dialog to a different file — the Prev/Next walker's entry point.
    /// Rebuilds every tab (MFC's ChangedData()), because the Archive Preview tab
    /// comes and goes with the file type, and keeps the user on the tab they were
    /// reading. Distinct from applyDetails(), which is the cheap partial refresh.
    void setDetails(const QCborMap& details) override;

signals:
    /// Ask the owning panel to fetch a link with this exact flag combination. The
    /// daemon builds it — the four pre-generated variants in the details map cannot
    /// express combinations such as "hashset + hostname".
    void requestEd2kLink(const QString& fileHash, bool hashset, bool sourceHint, bool html);

public slots:
    /// Re-populate only the tabs a Kad notes result can change (File Names +
    /// Comments), rather than rebuilding the whole sheet: a full rebuild would
    /// restart the archive scan and throw away the trees' scroll position.
    void applyDetails(const QCborMap& details) override;

    /// Show a link built by the daemon in response to requestEd2kLink().
    /// @param sourceHintAvailable  whether we have anything to advertise as a source.
    void applyEd2kLink(const QString& link, bool sourceHintAvailable);

private:
    void buildTabs(const QCborMap& details, int tabToSelect);

    QWidget* createGeneralTab(const QCborMap& details);
    QWidget* createFileNamesTab(const QCborMap& details);
    QWidget* createCommentsTab(const QCborMap& details);
    QWidget* createMediaInfoTab(const QCborMap& details);
    QWidget* createMetadataTab(const QCborMap& details);
    QWidget* createEd2kLinkTab(const QCborMap& details);
    QWidget* createArchivePreviewTab(const QCborMap& details);

    void updateEd2kLinkDisplay();

    // Fill the dynamic tab from a details map (used at build time and on refresh).
    void populateFileNames(const QCborMap& details);

    QTabWidget* m_tabs = nullptr;
    int         m_pendingTab = General;   ///< requested tab, until m_tabs exists

    // Dynamic-tab widgets, repopulated by applyDetails().
    QTreeWidget*   m_fileNamesTree      = nullptr;
    QLabel*        m_fileNamesEmptyLabel = nullptr;
    CommentsPanel* m_commentsPanel      = nullptr;

    // ED2K Link tab state
    QTextEdit* m_linkEdit       = nullptr;
    QCheckBox* m_chkHashset     = nullptr;
    QCheckBox* m_chkHostname    = nullptr;
    QCheckBox* m_chkHtml        = nullptr;
    QString    m_ed2kLink;          ///< plain variant, shown until a request returns
    QString    m_fileHash;
};

} // namespace eMule
