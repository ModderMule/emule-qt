#pragma once

/// @file KadIncomingGraph.h
/// @brief Search Details graph of one incoming Kad request. Not MFC.
///
/// We sit in the middle of someone else's lookup: the requester (left) asked us, we
/// (middle) answered with contacts closer to the target (right). Y is the XOR distance to
/// the target on a log scale (common prefix bits), closer at the bottom as in the lookup
/// graph; a dashed line marks our own distance, so contacts below it moved the lookup on.

#include "controls/KadLookupGraph.h"

#include <QCborMap>
#include <QIcon>
#include <QString>

#include <array>
#include <cstdint>
#include <vector>

namespace eMule {

class KadIncomingGraph : public KadGraphBase {
    Q_OBJECT

public:
    explicit KadIncomingGraph(QWidget* parent = nullptr);

    /// Show request @p row (of KadIncomingModel) with its graph reply (GetKadIncomingGraph);
    /// @p title names it, @p answer is the Answer column.
    void setRequest(const QCborMap& row, const QCborMap& graph, const QString& title, const QString& answer);
    /// Nothing selected: only the hint shows.
    void clear();

    [[nodiscard]] quint64 requestId() const { return m_requestId; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    [[nodiscard]] int hitTest(const QPoint& pos) const override;
    [[nodiscard]] QString toolTipText(int idx) const override;
    [[nodiscard]] QRect nodeRect(int idx) const override;

private:
    enum class Role : std::uint8_t { Asker, Own, Handed };
    struct Node {
        Role role = Role::Handed;
        QString id;                       ///< routing id hex; empty: unknown
        QString nodeId;                   ///< ed3k node ID when it differs
        QString addr;
        QString cc;
        int version = -1;
        std::array<std::uint32_t, 4> dist{};
        double closeness = 0;             ///< 128 - log2(distance); 128: the target itself
        int prefixBits = 0;               ///< leading bits shared with the target
        bool known = false;
        QRect rect;                       ///< set by layoutNodes
    };

    void layoutNodes();
    [[nodiscard]] int yOf(double closeness) const;
    [[nodiscard]] const QIcon& iconOf(const Node& n) const;
    [[nodiscard]] const Node* own() const;
    void paintAxis(QPainter& p) const;

    std::vector<Node> m_nodes;            ///< asker, own, then handed by closeness
    quint64 m_requestId = 0;
    QString m_title;
    QString m_answer;
    QString m_target;
    QString m_hint;                       ///< shown instead of a picture
    QString m_extra;                      ///< what the answer held besides contacts
    int m_handedCount = 0;
    int m_wanted = -1;
    int m_hot = -1;
    // plot frame, set by layoutNodes
    QRect m_plot;
    int m_lo = 0;                         ///< closeness at the top
    int m_hi = 128;                       ///< closeness at the bottom
    int m_labelHeight = 12;
    std::array<QIcon, 5> m_icons;         ///< green, blue, yellow, gray, own
};

} // namespace eMule
