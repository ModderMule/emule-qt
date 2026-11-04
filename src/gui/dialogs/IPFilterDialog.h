#pragma once

/// @file IPFilterDialog.h
/// @brief Tools > IP Filter... — view, delete, append and save ranges (MFC CIPFilterDlg, IDD_IPFILTER).

#include "controls/AbstractTableModel.h"
#include "dialogs/RememberedSizeDialog.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QString>

#include <array>

class QLabel;
class QPushButton;
class QTableView;

namespace eMule {

class IpcClient;

/// One range as the daemon sent it.
struct IpFilterRow {
    bool v6 = false;
    uint32 start4 = 0;              ///< host order
    uint32 end4 = 0;
    std::array<uint8, 16> start6{}; ///< network bytes
    std::array<uint8, 16> end6{};
    uint32 level = 0;
    uint32 hits = 0;
    QString desc;
    QString startText;
    QString endText;
};

class IpFilterModel : public AbstractTableModel<IpFilterRow> {
    Q_OBJECT

public:
    enum Column { ColStart, ColEnd, ColLevel, ColHits, ColDesc, ColCount };

    using AbstractTableModel::AbstractTableModel;

    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation o, int role) const override;
    void sort(int column, Qt::SortOrder order) override;

    /// Parse GetIPFilter's uncompressed list.
    static std::vector<IpFilterRow> parse(const QByteArray& text);
    /// Drop these rows (sorted ascending).
    void removeRowsAt(const std::vector<int>& rows);
    /// Addresses covered by all rows (v4 only, as MFC counts).
    [[nodiscard]] quint64 totalIPs() const;

protected:
    [[nodiscard]] int columnCountValue() const override { return ColCount; }

private:
    int m_sortColumn = -1;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};

class IPFilterDialog : public RememberedSizeDialog {
    Q_OBJECT

public:
    explicit IPFilterDialog(IpcClient* ipc, QWidget* parent = nullptr);
    ~IPFilterDialog() override;

private:
    void reload();
    void deleteSelected();
    void appendFile();
    void save();
    void copySelected();
    void showContextMenu(const QPoint& pos);
    void updateTotals();
    [[nodiscard]] std::vector<int> selectedRows() const;
    void setBusy(bool busy);

    bool m_busyCursor = false;

    IpcClient* m_ipc = nullptr;
    IpFilterModel* m_model = nullptr;
    QTableView* m_view = nullptr;
    QLabel* m_totalFilters = nullptr;
    QLabel* m_totalIps = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_appendBtn = nullptr;
    QPushButton* m_saveBtn = nullptr;
};

} // namespace eMule
