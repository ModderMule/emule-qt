#pragma once

/// @file PendingOpenQueue.h
/// @brief What arrived from outside the GUI before there was anywhere to put it.
///
/// A link clicked in a browser and a .nzb double-clicked in Finder both reach a cold
/// start *before* the IPC connection to the daemon exists — the import needs the
/// daemon's known-file lookup, and a .nzb travels to it as bytes. Acting immediately
/// is what produced the two failures this queue exists to prevent: a link dropped and
/// nothing at all happening, and a modal "Not connected to the eMule core" over a
/// window the user has not even seen yet.
///
/// Split out of ExternalLinkHandler so the waiting rules can be tested: that class
/// reaches MainWindow and cannot be linked into a test binary on its own.

#include <QList>
#include <QString>

#include <functional>
#include <utility>

namespace eMule {

/// One thing that arrived from outside and had nowhere to go yet.
struct PendingOpen {
    QString value;          ///< an eD2K/magnet link, or the path of a .nzb
    /// The kind has to travel with the value: both are strings, and they are
    /// replayed through different doors.
    bool isFile = false;
};

/// The waiting items, oldest first.
class PendingOpenQueue {
public:
    /// How many to keep. A cold start delivers one, and a file manager can open a
    /// selection; beyond a handful something is feeding us faster than the daemon can
    /// start, and stacking confirmation dialogs behind each other helps nobody.
    static constexpr qsizetype kMax = 16;

    /// Queue @p item. False when the queue is full and it was dropped.
    ///
    /// Deliberately not deduplicated: asking for the same thing twice while the daemon
    /// starts is a repeat request, and what turns it into a download is the importer's
    /// own known-file filter or the daemon's duplicate check — not this.
    bool push(PendingOpen item)
    {
        if (m_items.size() >= kMax)
            return false;
        m_items.append(std::move(item));
        return true;
    }

    /// Hand every waiting item to @p act, oldest first, leaving the queue empty.
    ///
    /// Emptied *before* the first call, never after the last: acting on one opens
    /// dialogs, a dialog spins the event loop, and the event loop can re-enter here —
    /// an item released twice prompts twice.
    void release(const std::function<void(const PendingOpen&)>& act)
    {
        const QList<PendingOpen> items = std::exchange(m_items, {});
        for (const PendingOpen& item : items)
            act(item);
    }

    [[nodiscard]] bool isEmpty() const { return m_items.isEmpty(); }
    [[nodiscard]] qsizetype size() const { return m_items.size(); }

private:
    QList<PendingOpen> m_items;
};

} // namespace eMule
