// SPDX-License-Identifier: MIT

#include <ui/terminal_view.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>

#include <app/log.hpp>

namespace consola {
namespace {

// Pixels either side of a divider that still count as a grab.
constexpr int kDividerGrab = 3;

// Collects an emulator's encoded output so one input action can be admitted as
// a single queue item (DIP 1.7 whole-action admission, 6.1 broadcast). The
// buffer is fixed-size and the callback never throws: libtsm invokes it inside
// its C callback path, where exceptions must not escape.
struct ByteSink {
    static constexpr std::size_t kCapacity = 4096;
    std::array<std::byte, kCapacity> bytes{};
    std::size_t size = 0;
    bool overflow = false;
};

void collect_bytes(void *context, std::span<const std::byte> bytes)
{
    auto *sink = static_cast<ByteSink *>(context);
    if (bytes.size() > ByteSink::kCapacity - sink->size) {
        sink->overflow = true;
        return;
    }
    std::ranges::copy(bytes, sink->bytes.begin() + sink->size);
    sink->size += bytes.size();
}

// Restores a destination's output callback even if encoding throws.
struct OutputCallbackGuard {
    TerminalEmulator &emulator;
    TerminalEmulator::OutputCallback callback;
    void *context;

    ~OutputCallbackGuard()
    {
        emulator.set_output_callback(callback, context);
    }
};

} // namespace

TerminalView::TerminalView(int x, int y, int w, int h, TerminalManager &manager,
                           TerminalStyle style, ShortcutMap shortcuts)
    : Fl_Group(x, y, w, h), manager_(manager), style_(std::move(style)),
      shortcuts_(std::move(shortcuts))
{
    box(FL_NO_BOX);
    end();
}

TerminalView::~TerminalView()
{
    // Clear callbacks on any still-live emulator, then detach the widgets from
    // the group so the unique_ptrs (not Fl_Group) destroy them.
    for (auto &entry : widgets_) {
        TerminalInstance *instance = manager_.find(entry.first);
        if (instance != nullptr) {
            instance->emulator().set_output_callback(nullptr, nullptr);
            instance->emulator().set_title_callback(nullptr, nullptr);
        }
        Fl_Group::remove(entry.second.get());
    }
}

std::optional<TerminalId> TerminalView::start(const SessionOptions &options)
{
    split_options_ = options;
    const std::optional<TerminalId> id =
        manager_.create_root_terminal(TerminalOptions{.session = options, .style = style_});
    if (!id) {
        return std::nullopt;
    }
    create_widgets();
    layout();
    sync_focus();
    return id;
}

bool TerminalView::pump()
{
    const bool added = create_widgets();
    const bool resized = w() != last_width_ || h() != last_height_;
    const TabId active_tab = manager_.tabs().active_id();
    const bool tab_changed = !last_tab_.has_value() || *last_tab_ != active_tab;
    const LayoutMetrics current_metrics = metrics();
    const bool metrics_changed =
        current_metrics.cell_width != last_metrics_.cell_width ||
        current_metrics.cell_height != last_metrics_.cell_height ||
        current_metrics.scrollbar_width != last_metrics_.scrollbar_width ||
        current_metrics.divider_thickness != last_metrics_.divider_thickness ||
        current_metrics.pane_border != last_metrics_.pane_border;
    if (added || resized || tab_changed || metrics_changed) {
        last_tab_ = active_tab;
        last_width_ = w();
        last_height_ = h();
        last_metrics_ = current_metrics;
        layout();
    }

    const bool dirty = manager_.pump();
    close_exited();
    if (destroy_stale_widgets()) {
        layout();
    }
    if (dirty) {
        for (auto &entry : widgets_) {
            if (entry.second->visible() != 0) {
                entry.second->redraw();
            }
        }
    }
    sync_focus();
    return manager_.terminal_count() != 0;
}

std::optional<TerminalId> TerminalView::split_focused(Orientation orientation)
{
    const std::optional<TerminalId> focused = manager_.focused();
    if (!focused || !can_split(*focused, orientation)) {
        return std::nullopt;
    }
    const std::optional<TerminalId> id = manager_.split_terminal(
        *focused, orientation, TerminalOptions{.session = split_options_, .style = style_});
    if (!id) {
        return std::nullopt;
    }
    manager_.focus(*id);
    create_widgets();
    layout();
    sync_focus();
    return id;
}

bool TerminalView::close_focused()
{
    const std::optional<TerminalId> focused = manager_.focused();
    if (!focused) {
        return false;
    }
    return manager_.close_terminal(*focused);
}

bool TerminalView::focus_direction(FocusDirection direction)
{
    const std::optional<TerminalId> focused = manager_.focused();
    if (!focused) {
        return false;
    }
    const std::optional<TerminalId> next = focus_neighbor(last_layout_, *focused, direction);
    if (!next) {
        return false;
    }
    focus(*next);
    return true;
}

void TerminalView::equalize()
{
    manager_.equalize();
    layout();
    redraw();
}

void TerminalView::focus(TerminalId id)
{
    manager_.focus(id);
    // focus() may select a different tab, so re-apply geometry before syncing
    // FLTK focus to the newly active tab's pane.
    last_tab_ = manager_.tabs().active_id();
    layout();
    sync_focus();
    update_window_title();
    redraw();
}

bool TerminalView::execute(const CommandRequest &request)
{
    switch (request.command) {
    case Command::SplitHorizontal:
        return split_focused(Orientation::horizontal).has_value();
    case Command::SplitVertical:
        return split_focused(Orientation::vertical).has_value();
    case Command::ClosePane:
        return close_focused();
    case Command::FocusLeft:
        return focus_direction(FocusDirection::left);
    case Command::FocusRight:
        return focus_direction(FocusDirection::right);
    case Command::FocusUp:
        return focus_direction(FocusDirection::up);
    case Command::FocusDown:
        return focus_direction(FocusDirection::down);
    case Command::ResizePane:
        return resize_focused(request.direction);
    case Command::NewTab:
        return new_tab().has_value();
    case Command::CloseTab:
        return close_tab(manager_.tabs().active_id());
    case Command::NextTab:
        return select_next_tab();
    case Command::PreviousTab:
        return select_previous_tab();
    case Command::ToggleBroadcast:
        set_broadcast(!broadcast_);
        return true;
    }
    return false;
}

void TerminalView::set_broadcast(bool enabled)
{
    if (broadcast_ == enabled) {
        return;
    }
    broadcast_ = enabled;
    log(LogLevel::Info, enabled ? "broadcast input enabled" : "broadcast input disabled");
}

std::vector<TerminalId> TerminalView::broadcast_targets(TerminalId source)
{
    const TerminalInstance *source_instance = manager_.find(source);
    if (source_instance == nullptr || !source_instance->input_enabled() ||
        !manager_.tab().layout().contains(source)) {
        return {};
    }
    if (!broadcast_) {
        return {source};
    }
    std::vector<TerminalId> targets;
    for (const TerminalId id : manager_.tab().layout().terminals()) {
        const TerminalInstance *instance = manager_.find(id);
        if (instance != nullptr && instance->input_enabled()) {
            targets.push_back(id);
        }
    }
    return targets;
}

void TerminalView::dispatch_key_input(TerminalId source, std::span<const KeyEvent> events)
{
    for (const TerminalId destination : broadcast_targets(source)) {
        const auto context = contexts_.find(destination);
        if (context == contexts_.end()) {
            continue;
        }
        TerminalEmulator &emulator = manager_.emulator(destination);
        // Encode the whole action first, then admit it as one queue item.
        ByteSink sink;
        emulator.set_output_callback(&collect_bytes, &sink);
        {
            OutputCallbackGuard guard{.emulator = emulator,
                                      .callback = &TerminalView::output_callback,
                                      .context = context->second.get()};
            for (const KeyEvent &event : events) {
                emulator.handle_keyboard(event);
            }
        }
        if (sink.overflow) {
            if (broadcast_) {
                log(LogLevel::Warning, "broadcast: key action too large for pane " +
                                           std::to_string(destination.value));
            }
            continue;
        }
        if (sink.size == 0) {
            continue;
        }
        const auto bytes = std::span<const std::byte>(sink.bytes.data(), sink.size);
        if (!manager_.send(destination, bytes) && broadcast_) {
            log(LogLevel::Warning,
                "broadcast: pane " + std::to_string(destination.value) + " rejected key input");
        }
    }
}

void TerminalView::dispatch_paste_input(TerminalId source, std::string_view text)
{
    for (const TerminalId destination : broadcast_targets(source)) {
        TerminalEmulator &emulator = manager_.emulator(destination);
        const PasteCapture capture = emulator.capture_paste(text, style_.max_paste_bytes);
        if (!capture.ok) {
            log(LogLevel::Warning, "paste rejected (oversized or empty)");
            continue;
        }
        const auto bytes = std::span<const std::byte>(
            reinterpret_cast<const std::byte *>(capture.bytes.data()), capture.bytes.size());
        if (!manager_.send(destination, bytes) && broadcast_) {
            log(LogLevel::Warning,
                "broadcast: pane " + std::to_string(destination.value) + " rejected paste input");
        }
    }
}

std::optional<TerminalId> TerminalView::new_tab()
{
    const TabId tab = manager_.create_tab();
    const std::optional<TerminalId> id = manager_.create_root_terminal(
        tab, TerminalOptions{.session = split_options_, .style = style_});
    if (!id) {
        // Roll back the empty tab so a failed launch does not leave a blank tab.
        manager_.close_tab(tab);
        return std::nullopt;
    }
    manager_.focus(*id);
    create_widgets();
    last_tab_ = manager_.tabs().active_id();
    layout();
    sync_focus();
    update_window_title();
    return id;
}

bool TerminalView::close_tab(TabId id)
{
    return manager_.close_tab(id);
}

bool TerminalView::select_tab(TabId id)
{
    if (!manager_.select_tab(id)) {
        return false;
    }
    last_tab_ = manager_.tabs().active_id();
    layout();
    sync_focus();
    update_window_title();
    redraw();
    return true;
}

bool TerminalView::select_next_tab()
{
    const std::optional<TabId> id = manager_.select_next_tab();
    if (!id) {
        return false;
    }
    last_tab_ = *id;
    layout();
    sync_focus();
    update_window_title();
    redraw();
    return true;
}

bool TerminalView::select_previous_tab()
{
    const std::optional<TabId> id = manager_.select_previous_tab();
    if (!id) {
        return false;
    }
    last_tab_ = *id;
    layout();
    sync_focus();
    update_window_title();
    redraw();
    return true;
}

bool TerminalView::resize_focused(FocusDirection direction)
{
    const std::optional<TerminalId> focused = manager_.focused();
    if (!focused) {
        return false;
    }
    const PaneRect *pane = nullptr;
    for (const PaneRect &candidate : last_layout_.panes) {
        if (candidate.terminal == *focused) {
            pane = &candidate;
            break;
        }
    }
    if (pane == nullptr) {
        return false;
    }

    const bool horizontal_axis =
        direction == FocusDirection::left || direction == FocusDirection::right;
    for (const DividerRect &divider : last_layout_.dividers) {
        const bool divider_vertical = divider.orientation == Orientation::vertical;
        if (horizontal_axis != divider_vertical) {
            continue;
        }
        bool adjacent = false;
        if (direction == FocusDirection::left) {
            adjacent = divider.rect.x + divider.rect.width == pane->logical.x;
        } else if (direction == FocusDirection::right) {
            adjacent = divider.rect.x == pane->logical.x + pane->logical.width;
        } else if (direction == FocusDirection::up) {
            adjacent = divider.rect.y + divider.rect.height == pane->logical.y;
        } else {
            adjacent = divider.rect.y == pane->logical.y + pane->logical.height;
        }
        if (!adjacent) {
            continue;
        }
        // Also require overlap on the perpendicular axis so a divider in
        // another region that happens to share the coordinate is not moved.
        if (horizontal_axis) {
            const bool overlap = divider.rect.y < pane->logical.y + pane->logical.height &&
                                 pane->logical.y < divider.rect.y + divider.rect.height;
            if (!overlap) {
                continue;
            }
        } else {
            const bool overlap = divider.rect.x < pane->logical.x + pane->logical.width &&
                                 pane->logical.x < divider.rect.x + divider.rect.width;
            if (!overlap) {
                continue;
            }
        }

        const LayoutMetrics current_metrics = metrics();
        const int step = horizontal_axis ? current_metrics.cell_width : current_metrics.cell_height;
        const int current = horizontal_axis ? divider.rect.x : divider.rect.y;
        const int delta =
            (direction == FocusDirection::left || direction == FocusDirection::up) ? -step : step;
        const SplitRatio ratio = split_ratio_for_position(
            manager_.tab().layout(), divider.split, divider.area, current + delta, current_metrics);
        // A nudge that is clamped by a subtree minimum must not overwrite the
        // stored ratio (DIP 3.3).
        if (ratio.clamped) {
            return false;
        }
        if (manager_.set_split_ratio(divider.split, ratio.ratio)) {
            layout();
            redraw();
            return true;
        }
    }
    return false;
}

TerminalWidget *TerminalView::widget(TerminalId id) noexcept
{
    const auto it = widgets_.find(id);
    return it == widgets_.end() ? nullptr : it->second.get();
}

void TerminalView::resize(int x, int y, int w, int h)
{
    // Fl_Group::resize scales children; the layout engine owns geometry, so
    // re-apply it after the group is resized.
    Fl_Group::resize(x, y, w, h);
    layout();
}

int TerminalView::handle(int event)
{
    switch (event) {
    case FL_PUSH: {
        const DividerRect *divider = divider_at(Fl::event_x() - x(), Fl::event_y() - y());
        if (divider != nullptr) {
            drag_ = DragState{.active = true,
                              .split = divider->split,
                              .area = divider->area,
                              .orientation = divider->orientation};
            return 1;
        }
        break;
    }
    case FL_DRAG:
        if (drag_.active) {
            const int position = drag_.orientation == Orientation::horizontal ? Fl::event_y() - y()
                                                                              : Fl::event_x() - x();
            const SplitRatio ratio = split_ratio_for_position(manager_.tab().layout(), drag_.split,
                                                              drag_.area, position, metrics());
            if (manager_.set_split_ratio(drag_.split, ratio.ratio)) {
                layout();
                redraw();
            }
            return 1;
        }
        break;
    case FL_RELEASE:
        if (drag_.active) {
            drag_.active = false;
            return 1;
        }
        break;
    default:
        break;
    }
    return Fl_Group::handle(event);
}

bool TerminalView::create_widgets()
{
    bool added = false;
    for (const TerminalId id : manager_.terminal_ids()) {
        if (!widgets_.contains(id)) {
            add_widget(id);
            added = true;
        }
    }
    return added;
}

bool TerminalView::destroy_stale_widgets()
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
    return !stale.empty();
}

void TerminalView::close_exited()
{
    // A pane whose process has exited closes itself (DIP 4.4). Ordinary child
    // exit always drains, so the loop has already fed the final output before
    // publishing `exited`; the pane is removed and the layout rebalances.
    for (const TerminalId id : manager_.terminal_ids()) {
        TerminalInstance *instance = manager_.find(id);
        if (instance != nullptr && instance->state() == TerminalState::exited &&
            !instance->closing()) {
            manager_.close_terminal(id);
        }
    }
    // Remove a tab that has become empty while other tabs remain; the last tab is
    // kept so the application can shut down cleanly once it is empty.
    if (manager_.tabs().count() > 1) {
        for (const TabId tab : manager_.tabs().ids()) {
            const Tab *found = manager_.tabs().find(tab);
            if (found != nullptr && found->terminal_count() == 0) {
                manager_.close_tab(tab);
            }
        }
    }
}

TerminalWidget *TerminalView::add_widget(TerminalId id)
{
    auto context = std::make_unique<Context>();
    context->view = this;
    context->id = id;
    Context *context_ptr = context.get();
    contexts_[id] = std::move(context);

    TerminalEmulator &emulator = manager_.emulator(id);
    auto widget = std::make_unique<TerminalWidget>(0, 0, 1, 1, emulator, style_);
    emulator.set_output_callback(&TerminalView::output_callback, context_ptr);
    emulator.set_title_callback(&TerminalView::title_callback, context_ptr);
    widget->set_paste_text_callback(&TerminalView::paste_text_callback, context_ptr);
    widget->set_resize_callback(&TerminalView::resize_callback, context_ptr);
    widget->set_focus_callback(&TerminalView::focus_callback, context_ptr);
    widget->set_key_callback(&TerminalView::key_callback, context_ptr);
    widget->set_key_input_callback(&TerminalView::key_input_callback, context_ptr);

    TerminalWidget *raw = widget.get();
    add(raw);
    widgets_.emplace(id, std::move(widget));
    return raw;
}

void TerminalView::remove_widget(TerminalId id)
{
    const auto it = widgets_.find(id);
    if (it == widgets_.end()) {
        return;
    }
    Fl_Group::remove(it->second.get());
    contexts_.erase(id);
    widgets_.erase(it);
}

void TerminalView::layout()
{
    if (widgets_.empty()) {
        last_layout_ = LayoutResult{};
        return;
    }
    const Rect content{0, 0, w(), h()};
    last_layout_ = compute_layout(manager_.tab().layout(), content, metrics());

    // Advertise the tree's minimum size to the window manager (DIP 3.3). The
    // window minimum includes any chrome above the view (e.g. the tab bar).
    const Size minimum = minimum_size(manager_.tab().layout(), metrics());
    if (minimum.width > 0 && minimum.height > 0 &&
        (minimum.width != last_min_width_ || minimum.height != last_min_height_)) {
        last_min_width_ = minimum.width;
        last_min_height_ = minimum.height;
        if (Fl_Window *win = window()) {
            win->size_range(minimum.width, minimum.height + top_inset_);
        }
    }

    // Only the active tab's panes are shown; widgets for other tabs are kept
    // (preserving their emulator state) but hidden until their tab is active.
    // Active widgets are left untouched so a redundant hide/show never steals
    // FLTK focus from the focused pane.
    std::unordered_set<TerminalId> active;
    for (const PaneRect &pane : last_layout_.panes) {
        active.insert(pane.terminal);
    }
    for (auto &entry : widgets_) {
        if (!active.contains(entry.first)) {
            entry.second->hide();
        } else if (entry.second->visible() == 0) {
            entry.second->show();
        }
    }
    for (const PaneRect &pane : last_layout_.panes) {
        const auto it = widgets_.find(pane.terminal);
        if (it == widgets_.end()) {
            continue;
        }
        // FLTK widget coordinates are window-relative, so the view-local layout
        // rectangle is translated by the view's window position. The logical
        // rectangle drives the grid (DIP 2.11); FLTK clips drawing to the window
        // when the logical canvas is larger than the content.
        it->second->resize(x() + pane.logical.x, y() + pane.logical.y, pane.logical.width,
                           pane.logical.height);
    }
    redraw();
}

void TerminalView::sync_focus()
{
    const std::optional<TerminalId> focused = manager_.focused();
    if (!focused) {
        return;
    }
    TerminalWidget *target = widget(*focused);
    if (target != nullptr && Fl::focus() != target) {
        target->take_focus();
    }
}

void TerminalView::update_window_title()
{
    Fl_Window *win = window();
    if (win == nullptr) {
        return;
    }
    // Retitle from the newly active tab's focused terminal, falling back to the
    // application name when that terminal has not set a title.
    std::string title = "consola";
    const std::optional<TerminalId> focused = manager_.focused();
    if (focused) {
        TerminalInstance *instance = manager_.find(*focused);
        if (instance != nullptr && !instance->emulator().title().empty()) {
            title = instance->emulator().title();
        }
    }
    win->copy_label(title.c_str());
    win->redraw();
}

const DividerRect *TerminalView::divider_at(int x, int y) const
{
    for (const DividerRect &divider : last_layout_.dividers) {
        if (x >= divider.rect.x - kDividerGrab &&
            x < divider.rect.x + divider.rect.width + kDividerGrab &&
            y >= divider.rect.y - kDividerGrab &&
            y < divider.rect.y + divider.rect.height + kDividerGrab) {
            return &divider;
        }
    }
    return nullptr;
}

bool TerminalView::can_split(TerminalId target, Orientation orientation) const
{
    // Reject a split that cannot fit the current content area before launching
    // (DIP 3.3): preview the tree with a placeholder leaf.
    LayoutTree preview = manager_.tab().layout();
    const TerminalId placeholder{std::numeric_limits<std::uint64_t>::max()};
    if (!preview.split_terminal(target, orientation, placeholder)) {
        return false;
    }
    const Size minimum = minimum_size(preview, metrics());
    return minimum.width <= w() && minimum.height <= h();
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
    result.scrollbar_width = style_.scrollbar ? std::max(4, result.cell_width / 4) : 0;
    result.pane_border = 0;
    result.divider_thickness = 2;
    return result;
}

void TerminalView::output_callback(void *context, std::span<const std::byte> bytes)
{
    auto *self = static_cast<Context *>(context);
    // Encoded output and feed() replies stay session-local; broadcast rejection
    // is reported at the key/paste dispatch layer instead.
    self->view->manager_.send(self->id, bytes);
}

void TerminalView::paste_text_callback(void *context, std::string_view text)
{
    auto *self = static_cast<Context *>(context);
    self->view->dispatch_paste_input(self->id, text);
}

void TerminalView::key_input_callback(void *context, std::span<const KeyEvent> events)
{
    auto *self = static_cast<Context *>(context);
    self->view->dispatch_key_input(self->id, events);
}

void TerminalView::resize_callback(void *context, int columns, int rows)
{
    auto *self = static_cast<Context *>(context);
    self->view->manager_.resize(self->id, columns, rows);
}

void TerminalView::title_callback(void *context, std::string_view title)
{
    auto *self = static_cast<Context *>(context);
    // Only the focused terminal of the active tab drives the window title; a
    // background pane must not retitle the window.
    const std::optional<TerminalId> focused = self->view->manager_.focused();
    if (!focused || *focused != self->id) {
        return;
    }
    Fl_Window *window = self->view->window();
    if (window == nullptr) {
        return;
    }
    const std::string text(title);
    window->copy_label(text.empty() ? "consola" : text.c_str());
    window->redraw();
}

void TerminalView::focus_callback(void *context)
{
    // Only record focus here: taking FLTK focus again would recurse through
    // FL_FOCUS (DIP 3.5). The window title follows the focused pane.
    auto *self = static_cast<Context *>(context);
    self->view->manager_.focus(self->id);
    self->view->update_window_title();
}

bool TerminalView::key_callback(void *context, int key, int state)
{
    auto *self = static_cast<Context *>(context);
    const std::optional<CommandRequest> request = self->view->shortcuts_.match(key, state);
    if (!request) {
        return false;
    }
    // A recognized shortcut is consumed even if the command cannot apply, so it
    // never leaks to the terminal (DIP 3.6).
    (void)self->view->execute(*request);
    return true;
}

} // namespace consola
