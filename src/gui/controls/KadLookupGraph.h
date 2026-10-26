#pragma once

/// @file KadLookupGraph.h
/// @brief Custom widget showing Kad search lookup distance graph.
///
/// Port of MFC CKadLookupGraph: the interesting nodes of one lookup plotted by
/// discovery order (X) vs XOR distance from target (Y), with arrows from the
/// node that reported each contact.

#include <QElapsedTimer>
#include <QIcon>
#include <QWidget>

#include <array>
#include <cstdint>
#include <vector>

namespace eMule {

/// One node entry in a lookup history, received from IPC.
struct LookupEntry {
    QString contactID;
    QString distance;
    std::array<uint32_t, 4> dist{};   // 128-bit distance, most significant chunk first
    std::vector<int> receivedFromIdx; // indexes into the entry list
    int64_t askedContactsAge = -1;    // ms since asked at snapshot time, -1 = never
    uint32_t respondedContact = 0;
    int64_t askedSearchItemAge = -1;  // ms since asked at snapshot time, -1 = never
    uint32_t respondedSearchItem = 0;
    uint8_t contactVersion = 0;
    bool providedCloser = false;
    bool forcedInteresting = false;
};

/// Distance graph widget matching the MFC Kad "Search Details" view.
class KadLookupGraph : public QWidget {
    Q_OBJECT

public:
    explicit KadLookupGraph(QWidget* parent = nullptr);

    /// Replace the shown lookup with a new snapshot (from IPC).
    void setLookup(std::vector<LookupEntry> entries, int searchType);

    /// Drop the lookup; only the axes stay.
    void clear();

    [[nodiscard]] bool hasEntries() const { return !m_entries.empty(); }

    void setAutoShowLookups(bool on) { m_autoShow = on; }

signals:
    /// Context menu toggled "Automatically show active searches".
    void autoShowLookupsToggled(bool on);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void layoutNodes();
    [[nodiscard]] int hitTest(const QPoint& pos) const;
    [[nodiscard]] int hotDrawIdx() const;
    [[nodiscard]] int64_t ageNow(int64_t age) const;
    [[nodiscard]] int nodeIconIdx(const LookupEntry& entry) const;
    [[nodiscard]] QString toolTipText(int drawIdx) const;
    void refreshToolTip();

    std::vector<LookupEntry> m_entries;
    std::vector<QRect> m_nodeRects;   // draw index i = entry hecount-1-i (newest first)
    QElapsedTimer m_snapshotAge;
    QString m_hotId;                  // hovered node, kept across snapshots
    std::array<QIcon, 4> m_nodeIcons; // green, blue, yellow, red
    QIcon m_iconSearch;
    QIcon m_iconStoreFile;
    QIcon m_iconStoreWord;
    QIcon m_ovlResponded;
    QIcon m_ovlNoResponse;
    QRect m_client;
    int m_labelHeight = 8;
    int m_topY = 0;
    int m_baseLineX = 0;
    int m_baseLineY = 0;
    int m_histWidth = 0;              // 0 = too small to draw
    int m_searchType = -1;
    bool m_autoShow = true;
};

} // namespace eMule
