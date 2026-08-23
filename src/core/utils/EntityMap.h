#pragma once

/// @file EntityMap.h
/// @brief Hash-map-backed list-manager base for externally-owned entities.
///
/// Like EntityList, but stores entities in an unordered_map keyed by Key and
/// guards all access with a QMutex. The lock is held across the add/remove hooks
/// and the forEach callback — matching SharedFileList's existing thread-safety
/// contract (publishing/UDP threads iterate while the main thread mutates).
///
/// The base derives from QObject but carries no Q_OBJECT macro (see EntityList.h
/// for why). Subclasses keep their own Q_OBJECT and signals, and override the
/// protected hooks. m_map / m_mutex are protected so subclasses' bespoke methods
/// (e.g. data-size walks, secondary-set checks) can access them directly.
///
/// @warning A subclass must NOT re-declare m_mutex. Shadowing it silently splits
/// m_map across two locks — half the class guarding one, half the other — which is
/// exactly the bug this comment exists to prevent recurring.
///
/// Lock contract for the hooks (keyFor / isDuplicate / onEntityAdded /
/// onEntityRemoved): they run with m_mutex already held, on a *non-recursive*
/// QMutex. They may touch m_map and other state guarded by the same lock, and
/// nothing else. No disk I/O, no signal emission, and no call back into count() /
/// findByKey() / forEach() / addEntity() / removeEntity() — each of those would
/// self-deadlock. Side effects that need any of that belong in the caller, after
/// the template method returns (MFC drops its own lock at the same point,
/// srchybrid/SharedFileList.cpp:699).

#include <QMutex>
#include <QObject>

#include <functional>
#include <unordered_map>

namespace eMule {

template<class Key, class T>
class EntityMap : public QObject {
public:
    using QObject::QObject;

    [[nodiscard]] int count() const
    {
        QMutexLocker locker(&m_mutex);
        return static_cast<int>(m_map.size());
    }

    [[nodiscard]] T* findByKey(const Key& key) const
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_map.find(key);
        return it != m_map.end() ? it->second : nullptr;
    }

    /// Thread-safe iteration — the lock is held for the duration of the callback.
    void forEach(const std::function<void(T*)>& callback) const
    {
        QMutexLocker locker(&m_mutex);
        for (auto& [key, entity] : m_map)
            callback(entity);
    }

protected:
    /// Template method (takes the lock): keyFor -> dup-check -> insert ->
    /// onEntityAdded(). Returns true if the entity was added.
    bool addEntity(T* entity)
    {
        if (!entity)
            return false;
        QMutexLocker locker(&m_mutex);
        Key key = keyFor(entity);
        if (isDuplicate(key, entity))
            return false;
        m_map[key] = entity;
        onEntityAdded(entity); // under lock — matches existing emit-in-lock behavior
        return true;
    }

    /// Template method (takes the lock): erase by keyFor -> onEntityRemoved().
    /// Returns true if the entity was present and removed.
    bool removeEntity(T* entity)
    {
        if (!entity)
            return false;
        QMutexLocker locker(&m_mutex);
        auto it = m_map.find(keyFor(entity));
        if (it == m_map.end())
            return false;
        m_map.erase(it);
        onEntityRemoved(entity); // under lock
        return true;
    }

    /// Drop every entity (takes the lock). The map does not own them, so this
    /// only forgets them — no deletion. Exists so subclasses never have to poke
    /// m_map directly just to empty it.
    void clearEntities()
    {
        QMutexLocker locker(&m_mutex);
        m_map.clear();
    }

    /// Derive the storage key from an entity (e.g. its file hash).
    [[nodiscard]] virtual Key keyFor(T* entity) const = 0;

    /// Default duplicate check: key already present. Override to add secondary
    /// checks (e.g. an "unshared" exclusion set).
    [[nodiscard]] virtual bool isDuplicate(const Key& key, T* /*entity*/) const
    {
        return m_map.contains(key);
    }

    /// Side-effect hooks — subclasses emit their signal and do bespoke work here.
    /// Invoked while the mutex is held.
    virtual void onEntityAdded(T* /*entity*/) {}
    virtual void onEntityRemoved(T* /*entity*/) {}

    mutable QMutex m_mutex;
    std::unordered_map<Key, T*> m_map;
};

} // namespace eMule
