#pragma once

/// @file Ed3kKadModels.h
/// @brief Lists of the ed3k page of the Kad window: the routing table and the lookups, rows
/// as the core sends them (GetEd3kKadContacts, GetEd3kKadLookups); and the incoming requests
/// of both Kad pages (GetKadIncoming, GetEd3kKadIncoming).

#include "controls/AbstractTableModel.h"
#include "utils/HostnameResolver.h"

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
class Ed3kKadTableModel : public AbstractTableModel<QCborMap> {
public:
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /// Replace the rows with a new snapshot, matched by "id" (no reset).
    void setRows(const QCborArray& rows);
    [[nodiscard]] const QCborMap& row(int r) const { return m_rows.at(static_cast<size_t>(r)); }
    /// The hostname column shows PTR names ("Resolve downloaders' hostnames").
    void setResolveHostnames(bool enabled);

protected:
    struct Column {
        QString title;
        /// text of a row; the sort value is `sort` or, without it, this text
        std::function<QString(const QCborMap&)> text;
        std::function<QVariant(const QCborMap&)> sort;
    };
    explicit Ed3kKadTableModel(QObject* parent);
    [[nodiscard]] int columnCountValue() const override { return static_cast<int>(m_columns.size()); }
    /// Column @p column names the address @p addressOf of a row once resolved; while on, the
    /// header reads @p header (empty: unchanged).
    void setHostnameColumn(int column, std::function<QString(const QCborMap&)> addressOf, const QString& header = {});
    /// Look up the listed rows' addresses; after setRows.
    void refreshHostnames();
    /// Address of a row for the resolver.
    [[nodiscard]] QString hostAddress(const QCborMap& m) const { return m_addressOf ? m_addressOf(m) : QString(); }
    std::vector<Column> m_columns;
    HostnameResolver m_resolver;

private:
    void hostnameChanged(const QString& address);
    int m_hostColumn = -1;
    std::function<QString(const QCborMap&)> m_addressOf;
    QString m_hostHeader;
};

/// The routing table, one row per node: Type, Node ID, Country, Family, Distance, Fails,
/// Check in.
class Ed3kKadContactsModel : public Ed3kKadTableModel {
    Q_OBJECT

public:
    enum Column { ColType = 0, ColId, ColCountry, ColFamily, ColDistance, ColFails, ColCheck, ColIp };
    explicit Ed3kKadContactsModel(QObject* parent = nullptr);
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /// Replace the rows (setRows) and look up the new addresses.
    void setContacts(const QCborArray& rows);

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

/// Requests other nodes sent us, newest first (rows of ops::kadIncoming): Time, Type, Key,
/// Name, From, Answer; ed3k adds IP (added last; the panel shows it before Answer). From:
/// flag + ip:port (eD2K) or flag + node ID (ed3k). Not MFC.
class KadIncomingModel : public Ed3kKadTableModel {
    Q_OBJECT

public:
    enum class Network { Ed2k, Ed3k };
    enum Column { ColTime = 0, ColType, ColKey, ColName, ColFrom, ColAnswer, ColIp /* ed3k only */ };
    explicit KadIncomingModel(Network network, QObject* parent = nullptr);
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /// Replace the rows; ages turn into clock times here. Looks up the new addresses.
    void setIncoming(const QCborArray& rows);

    /// The request id of a row (GetKadIncomingGraph).
    [[nodiscard]] std::uint64_t idAt(int row) const;
    /// Model row of a request, -1 if gone.
    [[nodiscard]] int rowOf(std::uint64_t id) const;
    /// Newest row whose answer handed out contacts, -1: none.
    [[nodiscard]] int newestWithContacts() const;
    /// "Type: name" (or the key) of a row, for the graph.
    [[nodiscard]] QString titleAt(int row) const;
    /// The Answer column of a row.
    [[nodiscard]] QString answerAt(int row) const { return answerText(this->row(row)); }

    /// kad::IncomingType as text.
    [[nodiscard]] static QString ed2kTypeText(int type);
    /// KadRequest.kind + 100 × op (0 find, 1 search, 2 store) as text.
    [[nodiscard]] static QString ed3kTypeText(int type);

private:
    [[nodiscard]] static QString answerText(const QCborMap& m);
    /// The asker's hostname once resolved, else its ip:port.
    [[nodiscard]] QString addressText(const QCborMap& m) const;
    /// Sort value of addressText: names by name, addresses numerically.
    [[nodiscard]] QVariant addressSort(const QCborMap& m) const;
    /// kad::IncomingRefusal as text; @p arg per code.
    [[nodiscard]] static QString refusalText(int code, int arg);
    Network m_network;
};

} // namespace eMule
