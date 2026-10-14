// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>

#include <FL/Fl_Window.H>

#include <app/session.hpp>
#include <app/terminal_instance.hpp>
#include <app/terminal_manager.hpp>
#include <app/terminal_style.hpp>
#include <layout/layout_geometry.hpp>
#include <ui/terminal_widget.hpp>

namespace consola {

// FLTK presentation for a TerminalManager's tab (DIP 3.4). It creates one
// TerminalWidget per terminal node, computes pane geometry with the layout
// engine and applies it to the widgets; widgets never own layout relationships.
// UI-thread only.
class TerminalView {
public:
    TerminalView(Fl_Window &window, TerminalManager &manager,
                 TerminalStyle style = default_terminal_style());
    ~TerminalView();

    TerminalView(const TerminalView &) = delete;
    TerminalView &operator=(const TerminalView &) = delete;
    TerminalView(TerminalView &&) = delete;
    TerminalView &operator=(TerminalView &&) = delete;

    // Create the root terminal and its widget.
    std::optional<TerminalId> start(const SessionOptions &options);

    // Pump the manager, create/destroy widgets for added/removed terminals and
    // apply geometry. Returns true while the tab still has terminals.
    bool pump();

    [[nodiscard]] TerminalManager &manager() noexcept
    {
        return manager_;
    }
    [[nodiscard]] TerminalWidget *widget(TerminalId id) noexcept;

private:
    struct Context {
        TerminalView *view = nullptr;
        TerminalId id;
    };

    void create_widgets();
    void destroy_stale_widgets();
    void layout();
    [[nodiscard]] LayoutMetrics metrics() const;
    TerminalWidget *add_widget(TerminalInstance &instance);
    void remove_widget(TerminalId id);

    static void output_callback(void *context, std::span<const std::byte> bytes);
    static void paste_callback(void *context, std::string_view bytes);
    static void resize_callback(void *context, int columns, int rows);
    static void title_callback(void *context, std::string_view title);

    Fl_Window &window_;
    TerminalManager &manager_;
    TerminalStyle style_;
    std::unordered_map<TerminalId, std::unique_ptr<TerminalWidget>> widgets_;
    // Heap-allocated so callback contexts have stable addresses.
    std::unordered_map<TerminalId, std::unique_ptr<Context>> contexts_;
    int last_width_ = -1;
    int last_height_ = -1;
};

} // namespace consola
