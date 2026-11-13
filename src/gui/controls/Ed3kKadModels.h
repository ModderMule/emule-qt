#pragma once

/// @file Ed3kKadModels.h
/// @brief Lists of the ed3k page of the Kad window: the routing table and the lookups, rows
/// as the core sends them (GetEd3kKadContacts, GetEd3kKadLookups).

#include <QAbstractTableModel>
#include <QCborArray>
#include <QCborMap>
#include <QIcon>
#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace eMule {

/// A table of CBOR map rows; subclasses name the columns.
class Ed3kKadTableModel : public QAbstractTableModel {
public:
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /// Replace the rows with a new snapshot.
    void setRows(const QCborArray& rows);
    void clear();
    [[nodiscard]] int count() const { return static_cast<int>(m_rows.size()); }
    [[nodiscard]] const QCborMap& row(int r) const { return m_rows.at(static_cast<size_t>(r)); }

protected:
    struct Column {
        QString title;
        /// text of a row; the sort value is `sort` or, without it, this text
        std::function<QString(const QCborMap&)> text;
        std::function<QVariant(const QCborMap&)> sort;
    };
    explicit Ed3kKadTableModel(QObject* parent);
    std::vector<Column> m_columns;

private:
    std::vector<QCborMap> m_rows;
};

/// The routing table, one row per node: Type, Node ID, Country, Family, Distance, Fails,
/// Check in.
class Ed3kKadContactsModel : public Ed3kKadTableModel {
    Q_OBJECT

public:
    enum Column { ColType = 0, ColId, ColCountry, ColFamily, ColDistance, ColFails, ColCheck };
    explicit Ed3kKadContactsModel(QObject* parent = nullptr);
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /// Version of a Hello.agent ("eMuleQt 0.7.25" -> "0.7.25"); the whole agent without one.
    [[nodiscard]] static QString versionOf(const QString& agent);

private:
    /// Contact0-4 by failed checks as eD2K Kad's type; SrcUnknown while not met since restart.
    [[nodiscard]] int contactImage(const QCborMap& m) const;
    std::array<QIcon, 6> m_icons;
};

/// Running and ended lookups: Number, Purpose, Name, Target, Kind, Load, Requests Sent, Asked,
/// Answered, Candidates, State.
class Ed3kKadLookupsModel : public Ed3kKadTableModel {
    Q_OBJECT

public:
    // enum Column { ColNumber = 0, ColPurpose, ColTarget, ColKind, ColAsked, ColAnswered, ColCandidates, ColState };
    enum Column {
        ColNumber = 0, ColPurpose, ColName, ColTarget, ColKind, ColLoad, ColSent, ColAsked, ColAnswered, ColCandidates, ColState
    };
    explicit Ed3kKadLookupsModel(QObject* parent = nullptr);
    /// The lookup id of a row.
    [[nodiscard]] std::uint64_t idAt(int row) const;
    /// Model row of a lookup, -1 if gone.
    [[nodiscard]] int rowOf(std::uint64_t id) const;
    /// KadRequest.kind as text (Protocol.md §10.3).
    [[nodiscard]] static QString kindText(int kind);
};

} // namespace eMule
