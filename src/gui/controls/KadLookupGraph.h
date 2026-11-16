#pragma once

/// @file KadLookupGraph.h
/// @brief Custom widget showing Kad search lookup distance graph.
///
/// Port of MFC CKadLookupGraph: the interesting nodes of one lookup plotted by
/// discovery order (X) vs XOR distance from target (Y), with arrows from the
/// node that reported each contact.

#include <QElapsedTimer>
#include <QIcon>
#include <QTimer>
#include <QWidget>

#include <array>
#include <cstdint>
#include <vector>

namespace eMule {

/// One node entry in a lookup history, received from IPC.
struct LookupEntry {
    QString contactID;
    QString distance;
    QString addr;                     // "ip:port", empty when unknown
    QString cc;                       // country code, empty when unknown
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

/// Shared by the Search Details graphs: the "Automatically show active searches" menu and
/// the node tool tips. A node's tip shows after a short rest on it (not Qt's wake-up
/// delay, which every pixel of movement restarts), follows the pointer and each new
/// snapshot, and says "Loading..." while a picture is on its way.
class KadGraphBase : public QWidget {
    Q_OBJECT

public:
    void setAutoShowLookups(bool on) { m_autoShow = on; }

    /// A new picture is on its way (or arrived: false). Under the pointer the tool tip
    /// says "Loading..." at once and turns into the node's tip with the data.
    void setLoading(bool on);

signals:
    /// Context menu toggled "Automatically show active searches".
    void autoShowLookupsToggled(bool on);

protected:
    explicit KadGraphBase(QWidget* parent);
    bool event(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

    /// Node under @p pos (widget coordinates), -1 = none.
    [[nodiscard]] virtual int hitTest(const QPoint& pos) const = 0;
    [[nodiscard]] virtual QString toolTipText(int idx) const = 0;
    [[nodiscard]] virtual QRect nodeRect(int idx) const = 0;

    /// Tip at @p globalPos: the node's, "Loading..." or none.
    void showTipAt(const QPoint& globalPos);
    /// New data: the tip under the pointer is rebuilt at once.
    void refreshTip();
    [[nodiscard]] bool isLoading() const;

    /// Tool tip table row, @p value escaped.
    [[nodiscard]] static QString tipRow(const QString& label, const QString& value);
    /// Tool tip table row, @p html as is.
    [[nodiscard]] static QString tipRowHtml(const QString& label, const QString& html);
    /// Country row with flag; empty when unknown. The address stays out of tooltips.
    [[nodiscard]] static QString countryRow(const QString& cc);

    bool m_autoShow = true;

private:
    void onDwell();

    QTimer m_dwell;                   // rest on a node before its tip shows
    QElapsedTimer m_loadingAge;
    int m_tipNode = -1;               // node under the pointer, -1 = none
    bool m_loading = false;
};

/// Distance graph widget matching the MFC Kad "Search Details" view.
class KadLookupGraph : public KadGraphBase {
    Q_OBJECT

public:
    explicit KadLookupGraph(QWidget* parent = nullptr);

    /// Replace the shown lookup with a new snapshot (from IPC).
    void setLookup(std::vector<LookupEntry> entries, int searchType);

    /// Drop the lookup; only the axes stay. @p loading: another one is on its way.
    void clear(bool loading = false);

    [[nodiscard]] bool hasEntries() const { return !m_entries.empty(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    [[nodiscard]] int hitTest(const QPoint& pos) const override;
    [[nodiscard]] QString toolTipText(int drawIdx) const override;
    [[nodiscard]] QRect nodeRect(int drawIdx) const override;

private:
    void layoutNodes();
    [[nodiscard]] int hotDrawIdx() const;
    [[nodiscard]] int64_t ageNow(int64_t age) const;
    [[nodiscard]] int nodeIconIdx(const LookupEntry& entry) const;

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
};

} // namespace eMule
