#pragma once

/// @file NzbFileChooserDialog.h
/// @brief Pick which files of one or more .nzb download, before they are queued.
///
/// Opened from the Add NZB dialog. The daemon parses each .nzb (InspectNzb) —
/// the GUI never links the NZB parser — and the files come back with the
/// indices AddNzb's skipped list names, plus an archive-set key so the volumes
/// of one set can be checked together.
///
/// Modal, but opened from a button click, never from an IPC callback; the
/// replies it waits on only fill the table, guarded by a QPointer.

#include <QCborMap>
#include <QDialog>
#include <QHash>
#include <QList>
#include <QStringList>

class QTreeWidget;
class QTreeWidgetItem;

namespace eMule {

class IpcClient;
class UsenetFileCheckList;

class NzbFileChooserDialog : public QDialog {
    Q_OBJECT

public:
    /// @p skipped is what an earlier visit chose, by .nzb path.
    NzbFileChooserDialog(IpcClient* ipc, const QStringList& paths,
                         const QHash<QString, QList<int>>& skipped,
                         QWidget* parent = nullptr);

    /// Unchecked file indices by .nzb path; valid after Accepted. A path with
    /// nothing unchecked is absent.
    [[nodiscard]] QHash<QString, QList<int>> skippedFiles() const { return m_result; }

    /// Fill one .nzb's rows from an InspectNzb reply. Public so a test can drive
    /// the table without a daemon.
    void applyInspection(const QString& path, const QCborMap& inspected);

    enum Column { ColName = 0, ColSize, ColCount };

private:
    void inspect(const QString& path);
    void showError(const QString& path, const QString& text);
    void onAccepted();
    [[nodiscard]] QTreeWidgetItem* nzbRow(const QString& path) const;

    IpcClient* m_ipc = nullptr;
    QHash<QString, QList<int>> m_initial;
    QHash<QString, QList<int>> m_result;

    QTreeWidget* m_tree = nullptr;
    UsenetFileCheckList* m_checks = nullptr;
};

} // namespace eMule
