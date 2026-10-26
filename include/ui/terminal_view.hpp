// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <FL/Fl_Group.H>

#include <app/session.hpp>
#include <app/terminal_manager.hpp>
#include <app/terminal_style.hpp>
#include <layout/layout_geometry.hpp>
#include <ui/commands.hpp>
#include <ui/shortcuts.hpp>
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
                 TerminalStyle style = default_terminal_style(),
                 ShortcutMap shortcuts = ShortcutMap{});
    ~TerminalView() override;

    TerminalView(const TerminalView &) = delete;
    TerminalView &operator=(const TerminalView &) = delete;
    TerminalView(TerminalView &&) = delete;
    TerminalView &operator=(TerminalView &&) = delete;

    // Create the root terminal and its widget.
    std::optional<TerminalId> start(const SessionOptions &options);

    // Pump the manager, create/destroy widgets for added/removed terminals and
    // apply geometry. Returns true while the application still has terminals in
    // any tab.
    bool pump();

    // Pane interaction (DIP 3.5). An explicit profile selects the profile for
    // the new pane; empty uses the active profile (DIP 6.4).
    std::optional<TerminalId> split_focused(Orientation orientation, std::string_view profile = {});
    bool close_focused();
    bool focus_direction(FocusDirection direction);
    void equalize();
    void focus(TerminalId id);

    // Tab interaction (DIP 4.2). new_tab() creates an empty tab with a root
    // terminal using the active/explicit profile; close_tab() closes a tab's
    // panes and removes the tab once empty. Selection applies geometry and
    // focus immediately.
    std::optional<TerminalId> new_tab(std::string_view profile = {});
    bool close_tab(TabId id);
    bool select_tab(TabId id);
    bool select_next_tab();
    bool select_previous_tab();

    // Rename the active tab (DIP 6.1). The label is trimmed; an empty label
    // clears the override and restores the positional fallback. Returns false
    // only when there is no active tab (never in practice).
    bool rename_active_tab(std::string_view title);

    // Runtime profile selection (DIP 6.4): the resolver maps a loaded profile
    // name to the session/style used by a new pane. Selecting a name outside
    // profile_names() fails once the list has been provided.
    using ProfileResolver = std::function<TerminalOptions(std::string_view)>;
    void set_profile_resolver(ProfileResolver resolver);
    void set_profile_names(std::vector<std::string> names);
    [[nodiscard]] const std::vector<std::string> &profile_names() const noexcept
    {
        return profile_names_;
    }
    bool select_profile(std::string_view name);
    [[nodiscard]] const std::string &active_profile() const noexcept
    {
        return active_profile_;
    }
    [[nodiscard]] bool profile_known(std::string_view name) const;
    // The profile assigned to a live pane (empty when it uses the view's
    // constructor defaults), for capture/restore (DIP 6.4, 6.5).
    [[nodiscard]] std::string profile_for(TerminalId id) const;

    // Execute a resolved command (DIP 3.6). Returns true when it applied.
    bool execute(const CommandRequest &request);

    // Broadcast input (DIP 6.1). When enabled, translated keys and explicit
    // pastes go to every input-enabled pane in the active tab, the focused pane
    // exactly once. Shortcuts, mouse events and scrollback stay local.
    void set_broadcast(bool enabled);
    [[nodiscard]] bool broadcast() const noexcept
    {
        return broadcast_;
    }
    // Destinations for input from `source`: `source` alone when broadcast is off,
    // otherwise the input-enabled panes of the active tab.
    [[nodiscard]] std::vector<TerminalId> broadcast_targets(TerminalId source);
    // Fan out already-translated key events / raw paste text on the UI thread.
    // Each destination encodes with its own VTE (DIP 6.1).
    void dispatch_key_input(TerminalId source, std::span<const KeyEvent> events);
    void dispatch_paste_input(TerminalId source, std::string_view text);

    // Pane zoom/maximize (DIP 6.1): temporarily show only the focused pane in
    // the active tab. Any focus change, split or tab switch clears it.
    void toggle_zoom();
    [[nodiscard]] bool zoomed() const noexcept
    {
        return zoomed_.has_value();
    }

    // Pane movement (DIP 6.1): swap the focused pane with its neighbor in
    // `direction`. Tab movement: move the active tab by `delta` (-1/+1).
    bool move_focused(FocusDirection direction);
    bool move_tab(int delta);

    [[nodiscard]] TerminalManager &manager() noexcept
    {
        return manager_;
    }
    [[nodiscard]] TerminalWidget *widget(TerminalId id) noexcept;

    // Vertical space above the view (e.g. a tab bar) that the advertised window
    // minimum must include (DIP 3.3).
    void set_top_inset(int inset) noexcept
    {
        top_inset_ = inset;
    }

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
    void close_exited();
    void layout();
    void sync_focus();
    void handle_focus(TerminalId id);
    void update_window_title();
    bool resize_focused(FocusDirection direction);
    [[nodiscard]] LayoutMetrics metrics() const;
    [[nodiscard]] LayoutMetrics metrics_for(TerminalId id) const;
    [[nodiscard]] const DividerRect *divider_at(int x, int y) const;
    [[nodiscard]] bool can_split(TerminalId target, Orientation orientation) const;
    struct ResolvedProfile {
        TerminalOptions options;
        std::string name;
    };
    [[nodiscard]] ResolvedProfile resolve_profile(std::string_view profile) const;
    void remember_profile(TerminalId id, const TerminalOptions &options, std::string profile);
    TerminalWidget *add_widget(TerminalId id);
    void remove_widget(TerminalId id);

    static void output_callback(void *context, std::span<const std::byte> bytes);
    static void paste_text_callback(void *context, std::string_view text);
    static void resize_callback(void *context, int columns, int rows);
    static void title_callback(void *context, std::string_view title);
    static void working_directory_callback(void *context, std::string_view path);
    static void bell_callback(void *context);
    static void focus_callback(void *context);
    static bool key_callback(void *context, int key, int state);
    static void key_input_callback(void *context, std::span<const KeyEvent> events);

    TerminalManager &manager_;
    TerminalStyle style_;
    ShortcutMap shortcuts_;
    SessionOptions split_options_;
    ProfileResolver profile_resolver_;
    std::vector<std::string> profile_names_;
    std::string active_profile_;
    // Per-pane presentation/profile assignment (DIP 6.4).
    std::unordered_map<TerminalId, TerminalStyle> terminal_styles_;
    std::unordered_map<TerminalId, std::string> terminal_profiles_;
    bool broadcast_ = false;
    std::optional<TerminalId> zoomed_;
    std::unordered_map<TerminalId, std::unique_ptr<TerminalWidget>> widgets_;
    // Heap-allocated so callback contexts have stable addresses.
    std::unordered_map<TerminalId, std::unique_ptr<Context>> contexts_;
    LayoutResult last_layout_;
    DragState drag_;
    std::optional<TabId> last_tab_;
    int last_width_ = -1;
    int last_height_ = -1;
    int last_min_width_ = 0;
    int last_min_height_ = 0;
    int top_inset_ = 0;
    LayoutMetrics last_metrics_{};
};

} // namespace consola
