// SPDX-License-Identifier: MIT

#include <ui/terminal_view.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace consola {

TerminalView::TerminalView(Fl_Window &window, TerminalManager &manager, TerminalStyle style)
    : window_(window), manager_(manager), style_(std::move(style))
{}

TerminalView::~TerminalView()
{
    // Remove every child from the group before the widgets are destroyed.
    for (auto &entry : widgets_) {
        window_.remove(entry.second.get());
    }
}

std::optional<TerminalId> TerminalView::start(const SessionOptions &options)
{
    const std::optional<TerminalId> id = manager_.create_root_terminal(TerminalOptions{options});
    if (!id) {
        return std::nullopt;
    }
    create_widgets();
    layout();
    return id;
}

bool TerminalView::pump()
{
    create_widgets();
    if (window_.w() != last_width_ || window_.h() != last_height_) {
        last_width_ = window_.w();
        last_height_ = window_.h();
        layout();
    }

    const bool dirty = manager_.pump();
    destroy_stale_widgets();
    if (dirty) {
        for (auto &entry : widgets_) {
            entry.second->redraw();
        }
    }
    return manager_.tab().terminal_count() != 0;
}

TerminalWidget *TerminalView::widget(TerminalId id) noexcept
{
    const auto it = widgets_.find(id);
    return it == widgets_.end() ? nullptr : it->second.get();
}

void TerminalView::create_widgets()
{
    for (TerminalInstance *instance : manager_.terminals()) {
        if (!widgets_.contains(instance->id())) {
            add_widget(*instance);
        }
    }
}

void TerminalView::destroy_stale_widgets()
{
    std::vector<TerminalId> stale;
    for (const auto &entry : widgets_) {
        if (manager_.find(entry.first) == nullptr) {
            stale.push_back(entry.first);
        }
    }
    for (const TerminalId id : stale) {
        remove_widget(id);
    }
}

TerminalWidget *TerminalView::add_widget(TerminalInstance &instance)
{
    auto context = std::make_unique<Context>();
    context->view = this;
    context->id = instance.id();
    Context *context_ptr = context.get();
    contexts_[instance.id()] = std::move(context);

    auto widget = std::make_unique<TerminalWidget>(0, 0, 1, 1, instance.emulator(), style_);
    instance.emulator().set_output_callback(&TerminalView::output_callback, context_ptr);
    instance.emulator().set_title_callback(&TerminalView::title_callback, context_ptr);
    widget->set_paste_callback(&TerminalView::paste_callback, context_ptr);
    widget->set_resize_callback(&TerminalView::resize_callback, context_ptr);

    TerminalWidget *raw = widget.get();
    window_.add(raw);
    widgets_.emplace(instance.id(), std::move(widget));
    return raw;
}

void TerminalView::remove_widget(TerminalId id)
{
    const auto it = widgets_.find(id);
    if (it == widgets_.end()) {
        return;
    }
    window_.remove(it->second.get());
    contexts_.erase(id);
    widgets_.erase(it);
}

void TerminalView::layout()
{
    if (widgets_.empty()) {
        return;
    }
    const Rect content{0, 0, window_.w(), window_.h()};
    const LayoutResult result = compute_layout(manager_.tab().layout(), content, metrics());
    for (const PaneRect &pane : result.panes) {
        const auto it = widgets_.find(pane.terminal);
        if (it == widgets_.end()) {
            continue;
        }
        // The logical rectangle drives the grid (DIP 2.11); FLTK clips drawing
        // to the window when the logical canvas is larger than the content.
        it->second->resize(pane.logical.x, pane.logical.y, pane.logical.width, pane.logical.height);
    }
    window_.redraw();
}

LayoutMetrics TerminalView::metrics() const
{
    LayoutMetrics result;
    if (widgets_.empty()) {
        return result;
    }
    const FontMetrics &font = widgets_.begin()->second->metrics();
    result.cell_width = std::max(1, static_cast<int>(std::lround(font.cell_width)));
    result.cell_height = std::max(1, font.cell_height);
    result.scrollbar_width = std::max(4, result.cell_width / 4);
    result.pane_border = 0;
    result.divider_thickness = 2;
    return result;
}

void TerminalView::output_callback(void *context, std::span<const std::byte> bytes)
{
    auto *self = static_cast<Context *>(context);
    self->view->manager_.send(self->id, bytes);
}

void TerminalView::paste_callback(void *context, std::string_view bytes)
{
    auto *self = static_cast<Context *>(context);
    self->view->manager_.send(
        self->id, std::span<const std::byte>(reinterpret_cast<const std::byte *>(bytes.data()),
                                             bytes.size()));
}

void TerminalView::resize_callback(void *context, int columns, int rows)
{
    auto *self = static_cast<Context *>(context);
    self->view->manager_.resize(self->id, columns, rows);
}

void TerminalView::title_callback(void *context, std::string_view title)
{
    auto *self = static_cast<Context *>(context);
    const std::string text(title);
    self->view->window_.copy_label(text.c_str());
    self->view->window_.redraw();
}

} // namespace consola
