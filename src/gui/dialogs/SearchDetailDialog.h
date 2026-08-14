#pragma once

/// @file SearchDetailDialog.h
/// @brief Detail dialog for a search result, matching MFC's
///        CSearchResultFileDetailSheet (srchybrid/SearchListCtrl.cpp:68-172).
///
/// A search hit has no local file behind it, so unlike FileDetailDialog this
/// sheet is only the Comments page — plus the Metadata page when extended
/// controls are on, exactly as the original decides it. See
/// docs/eMuleScreensGUI/SearchDetails.png.

#include "CommentsPanel.h"
#include "DetailDialog.h"

#include <QCborMap>

class QTabWidget;

namespace eMule {

class SearchDetailDialog : public DetailDialog {
    Q_OBJECT

public:
    /// Page indices. Metadata only exists under extended controls, so it is added
    /// first (as in MFC) and Comments shifts accordingly — use pageFor() rather
    /// than assuming a fixed index.
    enum Page { Comments, Metadata };

    explicit SearchDetailDialog(const QCborMap& details, Page initialPage = Comments,
                                QWidget* parent = nullptr);

    /// Swap to another search result — the Prev/Next walker's entry point.
    void setDetails(const QCborMap& details) override;

private:
    void buildPages(const QCborMap& details, Page pageToSelect);

    QTabWidget*    m_tabs          = nullptr;
    CommentsPanel* m_commentsPanel = nullptr;
    Page           m_pendingPage   = Comments;
};

} // namespace eMule
