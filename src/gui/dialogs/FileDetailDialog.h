#pragma once

/// @file FileDetailDialog.h
/// @brief Tabbed file-detail dialog (General / File Names / Comments / Media Info
///        / Metadata / ED2K Link / Archive Preview).

#include "IpcProtocol.h"

#include <QCborMap>
#include <QDialog>

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

/// Wire a dialog's searchKadNotes() to the daemon's SearchKadNotes request, shared by
/// every panel that opens the dialog. A rejection (Kad down, lookup already running) is
/// shown to the user, and the follow-up detail refreshes are armed only when the lookup
/// really started — otherwise the dialog would pretend a lookup was in flight.
/// @param detailsRequest  GetDownloadDetails for Transfers, GetSharedFileDetails for Shared.
void connectKadNotesSearch(FileDetailDialog* dialog, IpcClient* ipc,
                           Ipc::IpcMsgType detailsRequest);

/// Tabbed property dialog for a download file, matching the original MFC
/// FileDetailDialog. Shows General info, Source File Names, Comments,
/// Media Info, Metadata (ED2K tags), ED2K Link, and Archive Preview.
class FileDetailDialog : public QDialog {
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

signals:
    void searchKadNotes(const QString& fileHash, const QString& fileName);

    /// Ask the owning panel to fetch a link with this exact flag combination. The
    /// daemon builds it — the four pre-generated variants in the details map cannot
    /// express combinations such as "hashset + hostname".
    void requestEd2kLink(const QString& fileHash, bool hashset, bool sourceHint, bool html);

public slots:
    /// Re-populate the dynamic tabs (File Names + Comments) from a fresh details
    /// map. Called by the owning panel after a Kad search returns new results.
    void applyDetails(const QCborMap& details);

    /// Show a link built by the daemon in response to requestEd2kLink().
    /// @param sourceHintAvailable  whether we have anything to advertise as a source.
    void applyEd2kLink(const QString& link, bool sourceHintAvailable);

private:
    QWidget* createGeneralTab(const QCborMap& details);
    QWidget* createFileNamesTab(const QCborMap& details);
    QWidget* createCommentsTab(const QCborMap& details);
    QWidget* createMediaInfoTab(const QCborMap& details);
    QWidget* createMetadataTab(const QCborMap& details);
    QWidget* createEd2kLinkTab(const QCborMap& details);
    QWidget* createArchivePreviewTab(const QCborMap& details);

    void updateEd2kLinkDisplay();

    // Fill the dynamic tabs from a details map (used at build time and on refresh).
    void populateFileNames(const QCborMap& details);
    void populateComments(const QCborMap& details);

    QTabWidget* m_tabs = nullptr;

    // Dynamic-tab widgets, repopulated by applyDetails().
    QTreeWidget* m_fileNamesTree       = nullptr;
    QLabel*      m_fileNamesEmptyLabel  = nullptr;
    QTreeWidget* m_commentsTree        = nullptr;
    QLabel*      m_commentsEmptyLabel   = nullptr;

    // ED2K Link tab state
    QTextEdit* m_linkEdit       = nullptr;
    QCheckBox* m_chkHashset     = nullptr;
    QCheckBox* m_chkHostname    = nullptr;
    QCheckBox* m_chkHtml        = nullptr;
    QString    m_ed2kLink;          ///< plain variant, shown until a request returns
    QString    m_fileHash;
};

} // namespace eMule
