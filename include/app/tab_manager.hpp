// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <app/tab.hpp>

namespace consola {

// Application-wide tab identifier (DIP 4.1). Distinct from TerminalId, which is
// also application-wide but identifies terminals across every tab.
struct TabId {
    std::uint64_t value = 0;

    friend bool operator==(TabId, TabId) = default;
};

// Owns the ordered set of tabs and the active selection (DIP 4.1, 4.2). A tab
// owns its layout and terminal instances; the manager only orders them and
// tracks which one is active. It always holds at least one tab, so the active
// tab is always valid; removing the last tab replaces it with a fresh empty one.
class TabManager {
public:
    TabManager();

    [[nodiscard]] TabId active_id() const noexcept;
    [[nodiscard]] Tab &active() noexcept;
    [[nodiscard]] const Tab &active() const noexcept;

    [[nodiscard]] Tab *find(TabId id) noexcept;
    [[nodiscard]] const Tab *find(TabId id) const noexcept;

    [[nodiscard]] std::size_t count() const noexcept;
    [[nodiscard]] std::vector<TabId> ids() const;
    [[nodiscard]] std::optional<std::size_t> index_of(TabId id) const noexcept;

    // Append a new empty tab and make it active. Returns its id.
    TabId create();

    // Make `id` active. Returns false for an unknown id.
    bool select(TabId id);

    // Move the active selection with wraparound. Returns the new active id, or
    // nullopt when there is only one tab.
    std::optional<TabId> select_next();
    std::optional<TabId> select_previous();

    // Reorder a tab to `index` (clamped into range), keeping the same tab
    // active. Returns false for an unknown id.
    bool move(TabId id, std::size_t index);

    // Remove an empty tab. Returns false for an unknown id or a tab that still
    // owns terminals. Removing the last tab replaces it with a fresh empty one.
    bool remove(TabId id);

private:
    struct Entry {
        TabId id;
        std::unique_ptr<Tab> tab;
    };

    std::vector<Entry> entries_;
    std::size_t active_ = 0;
    std::uint64_t next_id_ = 1;
};

} // namespace consola

namespace std {

template <> struct hash<consola::TabId> {
    std::size_t operator()(const consola::TabId &id) const noexcept
    {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

} // namespace std
