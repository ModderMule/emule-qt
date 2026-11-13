#pragma once

/// @file ClientDetailDialog.h
/// @brief Client detail dialog showing General, Transfer, and Scores sections.

#include "DetailDialog.h"

#include <QCborMap>

namespace eMule {

/// Property dialog for a client, matching the original MFC CClientDetailDialog.
/// Shows General info, Transfer stats, and Scores in grouped sections.
class ClientDetailDialog : public DetailDialog {
    Q_OBJECT

public:
    /// Construct from a CBOR details map (as returned by GetClientDetails).
    explicit ClientDetailDialog(const QCborMap& details, QWidget* parent = nullptr);

    /// Swap the dialog to a different client — the Prev/Next walker's entry point.
    void setDetails(const QCborMap& details) override;

    /// Live refresh of the same client: rewrites the values in place, so a selection
    /// or scroll position survives. Falls back to a rebuild when the rows changed.
    void applyDetails(const QCborMap& details) override;

private:
    /// The page of @p details; a tab per network when the peer is known to both.
    [[nodiscard]] QWidget* buildPage(const QCborMap& details);
    [[nodiscard]] QWidget* buildContent(const QCborMap& details);
    /// An ed3k upload queue row (Ed3kUploadsBridge): node, Hello, country, its turn; no eD2K credits.
    [[nodiscard]] QWidget* buildEd3kContent(const QCborMap& details);

    QWidget* m_content = nullptr;
    QString  m_cc;   ///< country rows are flag widgets (both views), rebuilt when these change
};

} // namespace eMule
