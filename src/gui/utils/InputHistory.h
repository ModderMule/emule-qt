#pragma once

/// @file InputHistory.h
/// @brief Up/Down recall of sent lines for a chat input — MFC's ScrollHistory.
///
/// The IRC input (IrcWnd.cpp:458) and the friends chat input (ChatWnd.cpp:374) both
/// recall earlier lines with the arrow keys, per channel / per chat session. The
/// panel keeps one of these per session and routes Up/Down through scroll().

#include <QString>
#include <QStringList>

#include <optional>

namespace eMule {

class InputHistory {
public:
    /// Remember a sent line and reset the cursor to "new line".
    void add(const QString& text)
    {
        if (text.isEmpty())
            return;
        m_entries.append(text);
        if (m_entries.size() > kMaxEntries)
            m_entries.removeFirst();
        m_pos = -1;
    }

    /// Step through the history. Returns the text the input should show — empty
    /// when Down walks past the newest entry — or nullopt when there is none.
    [[nodiscard]] std::optional<QString> scroll(bool down)
    {
        if (m_entries.isEmpty())
            return std::nullopt;
        const auto last = static_cast<int>(m_entries.size()) - 1;
        if (!down) {
            if (m_pos < 0)
                m_pos = last;
            else if (m_pos > 0)
                --m_pos;
        } else if (m_pos >= 0 && m_pos < last) {
            ++m_pos;
        } else {
            m_pos = -1;
            return QString();
        }
        return m_entries.at(m_pos);
    }

private:
    static constexpr qsizetype kMaxEntries = 100;

    QStringList m_entries;
    int         m_pos = -1;
};

} // namespace eMule
