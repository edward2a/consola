// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>

#include <FL/Fl_Group.H>

#include <app/session.hpp>
#include <app/terminal_manager.hpp>
#include <app/terminal_style.hpp>
#include <layout/layout_geometry.hpp>
#include <ui/terminal_widget.hpp>

namespace consola {

// FLTK presentation for a TerminalManager's tab (DIP 3.4) and the pane
// interaction surface (DIP 3.5). It is a group whose children are one
// TerminalWidget per terminal node; the layout engine computes pane geometry
// and the view applies it, so widgets never own layout relationships. The view
// itself handles divider drag-resize and routes focus. UI-thread only.
class TerminalView : public Fl_Group {
public:
    TerminalView(int x, int y, int w, int h, TerminalManager &manager,
                 TerminalStyle style = default_terminal_style());
    ~TerminalView() override;

    TerminalView(const TerminalView &) = delete;
    TerminalView &operator=(const TerminalView &) = delete;
    TerminalView(TerminalView &&) = delete;
    TerminalView &operator=(TerminalView &&) = delete;

    // Create the root terminal and its widget.
    std::optional<TerminalId> start(const SessionOptions &options);

    // Pump the manager, create/destroy widgets for added/removed terminals and
    // apply geometry. Returns true while the tab still has terminals.
    bool pump();

    // Pane interaction (DIP 3.5).
    std::optional<TerminalId> split_focused(Orientation orientation);
    bool close_focused();
    bool focus_direction(FocusDirection direction);
    void equalize();
    void focus(TerminalId id);

    [[nodiscard]] TerminalManager &manager() noexcept
    {
        return manager_;
    }
    [[nodiscard]] TerminalWidget *widget(TerminalId id) noexcept;

    void resize(int x, int y, int w, int h) override;
    int handle(int event) override;

private:
    struct Context {
        TerminalView *view = nullptr;
        TerminalId id;
    };
    struct DragState {
        bool active = false;
        NodeId split;
        Rect area;
        Orientation orientation = Orientation::horizontal;
    };

    bool create_widgets();
    bool destroy_stale_widgets();
    void layout();
    void sync_focus();
    [[nodiscard]] LayoutMetrics metrics() const;
    [[nodiscard]] const DividerRect *divider_at(int x, int y) const;
    [[nodiscard]] bool can_split(TerminalId target, Orientation orientation) const;
    TerminalWidget *add_widget(TerminalId id);
    void remove_widget(TerminalId id);

    static void output_callback(void *context, std::span<const std::byte> bytes);
    static void paste_callback(void *context, std::string_view bytes);
    static void resize_callback(void *context, int columns, int rows);
    static void title_callback(void *context, std::string_view title);
    static void focus_callback(void *context);

    TerminalManager &manager_;
    TerminalStyle style_;
    SessionOptions split_options_;
    std::unordered_map<TerminalId, std::unique_ptr<TerminalWidget>> widgets_;
    // Heap-allocated so callback contexts have stable addresses.
    std::unordered_map<TerminalId, std::unique_ptr<Context>> contexts_;
    LayoutResult last_layout_;
    DragState drag_;
    int last_width_ = -1;
    int last_height_ = -1;
};

} // namespace consola
