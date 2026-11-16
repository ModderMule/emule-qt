#pragma once

/// @file ArchiveContentsDialog.h
/// @brief Transfers › Archive Contents: what the archives of a queued Usenet or hoster
/// download hold (hoster.md §6.1), and their files downloaded from ed3k by root.

#include "dialogs/RememberedSizeDialog.h"

#include <QCborMap>
#include <QString>

namespace eMule {

class ArchiveContentsView;
class IpcClient;

class ArchiveContentsDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    /// @p kind and @p payload as LookupArchiveContents takes them; @p folder: where
    /// entries go below incoming.
    ArchiveContentsDialog(IpcClient* ipc, const QString& title, const QString& kind, const QCborMap& payload,
                          const QString& folder, QWidget* parent = nullptr);

    [[nodiscard]] ArchiveContentsView* view() const { return m_view; }

private:
    ArchiveContentsView* m_view = nullptr;
};

} // namespace eMule
