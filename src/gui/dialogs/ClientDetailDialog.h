#pragma once

/// @file ClientDetailDialog.h
/// @brief Client detail dialog showing General, Transfer, and Scores sections.

#include <QCborMap>
#include <QDialog>

namespace eMule {

/// Property dialog for a client, matching the original MFC CClientDetailDialog.
/// Shows General info, Transfer stats, and Scores in grouped sections.
class ClientDetailDialog : public QDialog {
    Q_OBJECT

public:
    /// Construct from a CBOR details map (as returned by GetClientDetails).
    explicit ClientDetailDialog(const QCborMap& details, QWidget* parent = nullptr);
};

} // namespace eMule
