// SPDX-License-Identifier: MIT

#include <app/tab.hpp>

#include <utility>

namespace consola {

TerminalInstance *Tab::find(TerminalId id) noexcept
{
    const auto it = terminals_.find(id);
    return it == terminals_.end() ? nullptr : it->second.get();
}

const TerminalInstance *Tab::find(TerminalId id) const noexcept
{
    const auto it = terminals_.find(id);
    return it == terminals_.end() ? nullptr : it->second.get();
}

TerminalInstance *Tab::add_root(std::unique_ptr<TerminalInstance> instance)
{
    const TerminalId id = instance->id();
    if (!layout_.empty() || terminals_.contains(id)) {
        return nullptr;
    }
    TerminalInstance *raw = instance.get();
    terminals_.emplace(id, std::move(instance));
    layout_ = LayoutTree(id);
    return raw;
}

std::optional<NodeId> Tab::split_terminal(TerminalId target, Orientation orientation,
                                          std::unique_ptr<TerminalInstance> instance, float ratio)
{
    const TerminalId id = instance->id();
    if (terminals_.contains(id)) {
        return std::nullopt;
    }
    const std::optional<NodeId> leaf = layout_.split_terminal(target, orientation, id, ratio);
    if (!leaf) {
        return std::nullopt;
    }
    terminals_.emplace(id, std::move(instance));
    return leaf;
}

std::unique_ptr<TerminalInstance> Tab::remove_terminal(TerminalId id)
{
    const auto it = terminals_.find(id);
    if (it == terminals_.end()) {
        return nullptr;
    }
    layout_.remove(id);
    std::unique_ptr<TerminalInstance> instance = std::move(it->second);
    terminals_.erase(it);
    return instance;
}

std::vector<TerminalInstance *> Tab::terminals() noexcept
{
    std::vector<TerminalInstance *> out;
    out.reserve(terminals_.size());
    for (auto &entry : terminals_) {
        out.push_back(entry.second.get());
    }
    return out;
}

} // namespace consola
