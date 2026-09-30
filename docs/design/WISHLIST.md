# Wishlist

Optional ideas outside the current implementation plan. No stage or delivery
commitment is assigned.

## Native Wayland Support

v1 uses FLTK's X11 backend only. On Wayland desktops it requires XWayland;
native Wayland is not part of v1 or any current implementation stage.

Before promoting native Wayland support into the plan:

- explicitly approve FLTK's Cairo/Pango and other Wayland backend dependencies
- decide system vs bundled libdecor policy and document all required notices
- verify native clipboard, primary selection, IME, scaling and window behavior
- preserve X11 support and verify both backends without silent build fallbacks

## Terminal Search

Search visible output and retained scrollback:

- forward/backward search and wrap-around
- case-sensitive matching, with optional ASCII-only case-insensitive matching
- match highlighting and keyboard navigation

Deferred because search is not required for basic terminal use, and libtsm
4.8.0 has no public read-only offscreen-history iterator. Its drawing APIs
read the current viewport; scroll/draw/restore changes rendering ages and
clips rows to the current width. Search does not justify bringing the
planned fork forward.

Before promoting this idea into the plan:

- establish history access without disturbing viewport, selection or rendering
- define text-to-cell mapping and invalidation after output, resize and eviction
- keep matching toolkit-independent and on the UI-owned terminal-state path
- add headless history, wide-character, wrap-around and eviction tests

A future upstream API or a separately approved fork accessor may help. No
history-access extension or extra dependency is scheduled for search now.
