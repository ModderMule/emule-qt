#pragma once

/// @file PasswordFlagStore.h
/// @brief The per-indexer `password` flag counts, kept across daemon restarts.
///
/// One JSON file under `<configDir>/Indexers`. It holds counts only: an indexer
/// and a flag value are each written as a salted, truncated SHA-256, never as
/// text. The salt is random per installation, so the ids match nothing on another
/// machine. Not encryption: the values are short, and whoever holds the file and
/// its salt can test guesses.

#include "IndexerResult.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

class QTimer;

namespace eMule::indexer {

class PasswordFlagStore : public QObject {
public:
    /// An empty @p path keeps the counts in memory only.
    explicit PasswordFlagStore(const QString& path = {}, QObject* parent = nullptr);
    ~PasswordFlagStore() override;

    /// `<configDir>/Indexers/PasswordFlags.json`.
    [[nodiscard]] static QString defaultPath();

    /// Count @p page for the indexer @p slug, then clear the password marks of
    /// the values it sends on most rows. See PasswordFlagTally.
    void apply(const QString& slug, IndexerSearchPage& page);

    /// Drop what was learnt about @p slug, for an account the user removed.
    void forget(const QString& slug);

    /// Write now if anything changed. Also runs 5 s after a change and at teardown.
    void flush();

private:
    void load();
    void scheduleSave();
    [[nodiscard]] QString idFor(const QString& slug, const QString& flag = {}) const;

    QString m_path;
    QByteArray m_salt;
    QHash<QString, PasswordFlagTally> m_tallies;   ///< by indexer id
    QTimer* m_saveTimer = nullptr;
    bool m_dirty = false;
};

} // namespace eMule::indexer
