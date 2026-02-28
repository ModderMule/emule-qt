#pragma once

/// @file KadLookupGraph.h
/// @brief Custom widget showing Kad search lookup distance graph.
///
/// Replicates the MFC CKadLookupGraph: nodes discovered during a search
/// plotted by discovery order (X) vs XOR distance from target (Y),
/// connected by arrow lines showing discovery relationships.

#include <QWidget>

#include <cstdint>
#include <vector>

namespace eMule {

/// One node entry in a lookup history, received from IPC.
struct LookupEntry {
    QString contactID;
    QString distance;
    uint32_t dist[4] = {};            // 128-bit distance as 4x uint32 chunks
    std::vector<int> receivedFromIdx;
    uint32_t askedContactsTime = 0;
    uint32_t respondedContact = 0;
    uint32_t askedSearchItemTime = 0;
    uint32_t respondedSearchItem = 0;
    uint8_t contactVersion = 0;
    bool providedCloser = false;
    bool forcedInteresting = false;
};

/// Distance graph widget matching the MFC Kad "Search Details" tab.
class KadLookupGraph : public QWidget {
    Q_OBJECT

public:
    explicit KadLookupGraph(QWidget* parent = nullptr);

    /// Replace all entries with a new snapshot (from IPC).
    void setEntries(std::vector<LookupEntry> entries);

    /// Clear the graph.
    void clear();

    /// Set the title shown in the tab (e.g. "Node Lookup").
    void setTitle(const QString& title);

    [[nodiscard]] bool hasEntries() const { return !m_entries.empty(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    struct NodeRect {
        QRect rect;
        int entryIdx = -1;
    };

    int hitTest(const QPoint& pos) const;
    QColor nodeColor(const LookupEntry& entry) const;

    std::vector<LookupEntry> m_entries;
    std::vector<NodeRect> m_nodeRects;
    QRect m_selfRect;          // hit-test rect for the green self node
    QString m_title;
    int m_hotIdx = -1;
};

} // namespace eMule
