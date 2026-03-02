#pragma once

/// @file KadSearchesModel.h
/// @brief Table model for the "Current Searches" list in the Kad tab.

#include <QAbstractTableModel>
#include <QString>

#include <cstdint>
#include <vector>

namespace eMule {

/// Row data for one active Kad search.
struct KadSearchRow {
    uint32_t searchId = 0;
    QString key;
    QString type;
    QString name;
    QString status;
    float load = 0.0f;
    uint32_t packetsSent = 0;
    uint32_t requestAnswers = 0;
    uint32_t responses = 0;
};

/// Table model backing the "Current Searches" table in the Kad tab.
class KadSearchesModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        ColNumber = 0,
        ColKey,
        ColType,
        ColName,
        ColStatus,
        ColLoad,
        ColPacketsSent,
        ColResponses,
        ColCount
    };

    explicit KadSearchesModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Replace all searches with a new snapshot.
    void setSearches(std::vector<KadSearchRow> searches);

    /// Clear all searches.
    void clear();

    [[nodiscard]] int searchCount() const { return static_cast<int>(m_searches.size()); }

private:
    std::vector<KadSearchRow> m_searches;
};

} // namespace eMule
