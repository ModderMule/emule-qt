#pragma once

/// @file FileDetailDialog.h
/// @brief Tabbed file-detail dialog (General / File Names / Comments / Media Info
///        / Metadata / ED2K Link / Archive Preview).

#include <QCborMap>
#include <QDialog>

class QCheckBox;
class QLabel;
class QTabWidget;
class QTextEdit;
class QTreeWidget;

namespace eMule {

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

public slots:
    /// Re-populate the dynamic tabs (File Names + Comments) from a fresh details
    /// map. Called by the owning panel after a Kad search returns new results.
    void applyDetails(const QCborMap& details);

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
    QString    m_ed2kLink;
    QString    m_ed2kLinkHashset;
    QString    m_ed2kLinkHTML;
    QString    m_ed2kLinkHostname;
};

} // namespace eMule
