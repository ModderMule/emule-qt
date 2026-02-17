#pragma once

/// @file ContainerUtils.h
/// @brief Container utilities and MFC collection migration helpers.
///
/// Header-only, no Qt dependency.
///
/// MFC → C++ Standard Library migration table:
///   CArray<T>         → std::vector<T>
///   CList<T>          → std::list<T>
///   CMap<K,V>         → std::unordered_map<K,V>
///   CMapStringToOb    → std::unordered_map<std::string, T*>
///   CMapStringToPtr   → std::unordered_map<std::string, void*>
///   CTypedPtrList      → std::list<T*> or std::vector<T*>
///   CTypedPtrArray      → std::vector<T*>
///   CTypedPtrMap        → std::unordered_map<K, V*>
///   CStringArray       → std::vector<QString>  (or QStringList)
///   CStringList        → QStringList
///   CObList            → std::list<QObject*>

#include <algorithm>
#include <concepts>
#include <functional>
#include <ranges>

namespace eMule {

/// Sort a range in ascending order (natural < comparison).
template <std::ranges::random_access_range R>
constexpr void sortAscending(R&& range)
{
    std::ranges::sort(std::forward<R>(range));
}

/// Sort a range using a custom comparator or projection.
template <std::ranges::random_access_range R, typename Comp>
constexpr void sortBy(R&& range, Comp comp)
{
    std::ranges::sort(std::forward<R>(range), std::move(comp));
}

/// Binary search in a sorted range.  Returns an iterator to the element
/// if found, or the end iterator if not present.
template <std::ranges::forward_range R, typename T>
[[nodiscard]] constexpr auto binaryFind(R&& range, const T& value)
{
    auto it = std::ranges::lower_bound(range, value);
    if (it != std::ranges::end(range) && !(*it < value) && !(value < *it))
        return it;
    return std::ranges::end(range);
}

/// Erase all elements matching @p pred from a container (calls std::erase_if).
template <typename Container, typename Pred>
constexpr auto eraseIf(Container& container, Pred pred)
{
    return std::erase_if(container, std::move(pred));
}

} // namespace eMule
