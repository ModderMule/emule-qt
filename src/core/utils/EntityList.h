#pragma once

/// @file EntityList.h
/// @brief Vector-backed list-manager base for externally-owned entity pointers.
///
/// Hoists the add/remove/find/forEach/count/duplicate-check skeleton shared by
/// the core collections (ClientList, DownloadQueue) into one place. Subclasses
/// keep their own Q_OBJECT and signals, and override the protected hooks to emit
/// the signal plus run any side-effects (sorting, logging, counters).
///
/// The base derives from QObject but carries no Q_OBJECT macro — templates can't
/// declare one, and it needs none (it adds no meta members). moc resolves a
/// concrete subclass's base staticMetaObject through inheritance to QObject's.

#include <QObject>

#include <algorithm>
#include <functional>
#include <vector>

namespace eMule {

template<class T>
class EntityList : public QObject {
public:
    using QObject::QObject;

    [[nodiscard]] int count() const { return static_cast<int>(m_items.size()); }

    [[nodiscard]] bool contains(const T* e) const
    {
        return std::find(m_items.begin(), m_items.end(), e) != m_items.end();
    }

    void forEach(const std::function<void(T*)>& callback) const
    {
        for (auto* e : m_items)
            callback(e);
    }

    [[nodiscard]] const std::vector<T*>& items() const { return m_items; }

protected:
    /// Template method: null-check -> dup-check -> append -> onEntityAdded().
    /// Returns true if the entity was added.
    bool addEntity(T* entity, bool skipDupCheck = false)
    {
        if (!entity)
            return false;
        if (!skipDupCheck && isDuplicate(entity))
            return false;
        m_items.push_back(entity);
        onEntityAdded(entity);
        return true;
    }

    /// Template method: find -> erase -> onEntityRemoved().
    /// Returns true if the entity was present and removed.
    bool removeEntity(T* entity)
    {
        if (!entity)
            return false;
        auto it = std::find(m_items.begin(), m_items.end(), entity);
        if (it == m_items.end())
            return false;
        m_items.erase(it);
        onEntityRemoved(entity);
        return true;
    }

    /// Default duplicate check: pointer identity (matches ClientList).
    /// Override for hash-based or other domain-specific detection.
    [[nodiscard]] virtual bool isDuplicate(const T* entity) const { return contains(entity); }

    /// Side-effect hooks — subclasses emit their signal and do bespoke work here.
    virtual void onEntityAdded(T* /*entity*/) {}
    virtual void onEntityRemoved(T* /*entity*/) {}

    std::vector<T*> m_items;
};

} // namespace eMule
