#pragma once

/// @file Ed3kPlainCopies.h
/// @brief The opened plain copies of sealed set members (hidden-set.md §4.2, subscription.md
/// §8.1, batch 15 #12 MUST): never shared, no record, not served. The sealed copy is what is
/// seeded. A saved registry (Config/ed3k/plain.json, owner-only: member paths of a hidden
/// set tell what it holds) that the share list (SharedFileList::setNeverShared), Ed3kShared
/// and the completed-download hook ask. Where either copy is kept is local (Ed3kSets).
///
/// Thread-safe reads: the share list may ask from its scan.

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

namespace eMule::ed3k {

class Ed3kPlainCopies : public QObject {
    Q_OBJECT

public:
    /// @p configDir: Config/ (ed3k/plain.json below it), loaded here.
    explicit Ed3kPlainCopies(const QString& configDir, QObject* parent = nullptr);
    ~Ed3kPlainCopies() override;

    /// @p path (a plain copy, before or after it is written) is never shared; saved.
    void add(const QString& path);
    [[nodiscard]] bool contains(const QString& path) const;
    [[nodiscard]] QStringList paths() const;
    [[nodiscard]] QString statePath() const;
    /// absolute, clean, case-folded (as SharedFileList::pathKey)
    [[nodiscard]] static QString keyOf(const QString& path);

signals:
    /// A new plain copy: a share list holding it drops it.
    void added(const QString& path);

private:
    void load();
    void save() const;

    QString m_configDir;
    mutable QMutex m_mutex;
    QHash<QString, QString> m_paths;   ///< key → path as given
};

} // namespace eMule::ed3k
