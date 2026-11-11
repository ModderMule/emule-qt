#pragma once

/// @file LandedFiles.h
/// @brief Files a download engine put into incoming, remembered across restarts (#177).
///
/// Hoster downloads (and what their archives held) land in incoming, which is always
/// shared. With ed3kHosterShareFiles off they stay out of the eD2K / ed3k share:
/// SharedFileList::setNeverShared asks blocks(). Recorded whether sharing is on or not,
/// so switching it off later covers earlier downloads too.

#include <QSet>
#include <QString>
#include <QStringList>

namespace eMule {

class LandedFiles {
public:
    /// @p listPath: one path per line; written only once something landed (#105)
    explicit LandedFiles(QString listPath = {});

    void setListPath(const QString& listPath) { m_listPath = listPath; }
    /// Reads the list (replaces what is held).
    void load();
    /// Adds @p paths and saves the list.
    void add(const QStringList& paths);
    [[nodiscard]] bool contains(const QString& filePath) const;
    [[nodiscard]] qsizetype size() const { return m_keys.size(); }

    /// On: blocks() names the landed files. False when unchanged.
    bool setBlocking(bool on);
    [[nodiscard]] bool blocking() const { return m_blocking; }
    [[nodiscard]] bool blocks(const QString& filePath) const { return m_blocking && contains(filePath); }

    /// Oldest entries go beyond this many
    static constexpr qsizetype kMaxEntries = 200'000;

private:
    void save() const;

    QString m_listPath;
    QStringList m_paths;      ///< as given, oldest first
    QSet<QString> m_keys;     ///< SharedFileList::pathKey of each
    bool m_blocking = false;
};

} // namespace eMule
