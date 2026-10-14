// SPDX-License-Identifier: MIT

#include <app/tab_manager.hpp>

#include <cstddef>
#include <utility>

namespace consola {

TabManager::TabManager()
{
    // A window always has at least one tab, so active() is always valid.
    create();
}

TabId TabManager::active_id() const noexcept
{
    return entries_[active_].id;
}

Tab &TabManager::active() noexcept
{
    return *entries_[active_].tab;
}

const Tab &TabManager::active() const noexcept
{
    return *entries_[active_].tab;
}

Tab *TabManager::find(TabId id) noexcept
{
    for (Entry &entry : entries_) {
        if (entry.id == id) {
            return entry.tab.get();
        }
    }
    return nullptr;
}

const Tab *TabManager::find(TabId id) const noexcept
{
    for (const Entry &entry : entries_) {
        if (entry.id == id) {
            return entry.tab.get();
        }
    }
    return nullptr;
}

std::size_t TabManager::count() const noexcept
{
    return entries_.size();
}

std::vector<TabId> TabManager::ids() const
{
    std::vector<TabId> out;
    out.reserve(entries_.size());
    for (const Entry &entry : entries_) {
        out.push_back(entry.id);
    }
    return out;
}

std::optional<std::size_t> TabManager::index_of(TabId id) const noexcept
{
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].id == id) {
            return i;
        }
    }
    return std::nullopt;
}

TabId TabManager::create()
{
    Entry entry;
    entry.id = TabId{next_id_++};
    entry.tab = std::make_unique<Tab>();
    const TabId id = entry.id;
    entries_.push_back(std::move(entry));
    active_ = entries_.size() - 1;
    return id;
}

bool TabManager::select(TabId id)
{
    const std::optional<std::size_t> index = index_of(id);
    if (!index) {
        return false;
    }
    active_ = *index;
    return true;
}

std::optional<TabId> TabManager::select_next()
{
    if (entries_.size() < 2) {
        return std::nullopt;
    }
    active_ = (active_ + 1) % entries_.size();
    return entries_[active_].id;
}

std::optional<TabId> TabManager::select_previous()
{
    if (entries_.size() < 2) {
        return std::nullopt;
    }
    active_ = (active_ + entries_.size() - 1) % entries_.size();
    return entries_[active_].id;
}

bool TabManager::move(TabId id, std::size_t index)
{
    const std::optional<std::size_t> from = index_of(id);
    if (!from) {
        return false;
    }
    if (index >= entries_.size()) {
        index = entries_.size() - 1;
    }
    const TabId active_id = entries_[active_].id;
    Entry entry = std::move(entries_[*from]);
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(*from));
    entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(index), std::move(entry));
    active_ = index_of(active_id).value_or(0);
    return true;
}

bool TabManager::remove(TabId id)
{
    const std::optional<std::size_t> index = index_of(id);
    if (!index || entries_[*index].tab->terminal_count() != 0) {
        return false;
    }
    const TabId active_id = entries_[active_].id;
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(*index));
    if (entries_.empty()) {
        create();
        return true;
    }
    active_ = index_of(active_id).value_or(entries_.size() - 1);
    return true;
}

} // namespace consola
