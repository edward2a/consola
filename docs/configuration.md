# Configuration

consola reads a single YAML file. The default location is
`$XDG_CONFIG_HOME/consola/config.yml` (`~/.config/consola/config.yml` when
`XDG_CONFIG_HOME` is unset); a path given on the command line overrides it. A
missing file is not an error: the built-in defaults apply.

The schema is versioned. Every file must start with:

```yaml
version: 1
```

A file whose `version` is missing or not understood is rejected as a whole; no
partial configuration is applied. An entirely empty file — or one containing
only whitespace and comments — is treated as the built-in defaults.

## Precedence

Settings resolve in three layers, lowest first:

1. **Built-in defaults** (defined in C++, `include/config/config.hpp`).
2. **`general`**, plus the top-level `font` and `colours` sections.
3. The selected **profile** (`profiles.<name>`).

A key present in a profile replaces the value from the layers below; a key that
is absent inherits. Profiles therefore store only the settings they override,
so a partial palette override is lossless. Only per-pane settings can be
overridden by a profile: `shell`, `arguments`, `working_directory`,
`login_shell`, `term`, `colorterm`, `environment`, `scrollback`, `font`,
`word_chars`, `bell`, `cursor` and `colours`. Everything else in `general` is
application-wide.

`default_profile` (default `default`) names the profile new panes use. It must
name a profile that exists in `profiles`; the built-in `default` profile always
exists, even when the file does not define it. A `default_profile` naming a
missing profile is an error.

Unknown keys are reported as warnings and ignored. Invalid values are errors.

## Top-level keys

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `version` | integer | — | Required; must be `1`. |
| `general` | map | see below | Application-wide settings. |
| `font` | map | see below | Default terminal font. |
| `colours` | map | see below | Default palette and foreground/background. |
| `default_profile` | string | `default` | Profile new panes use. |
| `profiles` | map of name → profile | `default` | Named per-pane overrides. |
| `shortcuts` | map | see below | Global keyboard shortcuts. |
| `layouts` | map of name → layout | `{}` | Named saved layouts (data only). |

## `general`

Boolean values are `true` or `false`. Integer and size fields accept decimal
digits only.

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `shell` | string | `$SHELL` (or `/bin/sh`) | Program launched in a new pane; empty uses `$SHELL`. |
| `arguments` | list of string | `[]` | Extra argv passed to `shell`. |
| `working_directory` | string | inherited | Start directory; empty means the process CWD. |
| `login_shell` | bool | `false` | Launch the shell as a login shell. |
| `term` | string | `xterm-256color` | Value of `TERM`. |
| `colorterm` | string | `truecolor` | Value of `COLORTERM`. |
| `environment` | map of string | `{}` | Extra child environment variables; empty inherits consola's environment. |
| `scrollback` | integer ≥ 0 | `10000` | Scrollback lines; `0` disables scrollback. |
| `scrollbar` | bool | `true` | Show the scrollback scrollbar. |
| `wheel_lines` | integer > 0 | `3` | Lines scrolled per wheel notch. |
| `alternate_scroll` | bool | `true` | Wheel sends arrow keys on the alternate screen. |
| `backspace_sends_delete` | bool | `true` | Send DEL (0x7f) instead of BS (0x08). |
| `bell` | string | `none` | One of `none`, `audible`, `visual`, `both`. Audible uses the system beep (coalesced); visual flashes the pane. |
| `cursor` | map | see below | Cursor shape and blink. |
| `word_chars` | string | `-A-Za-z0-9,./?%&#:_=+@~` | Characters that form a word for double-click selection. |
| `kill_timeout_ms` | integer, 1–600000 | `2000` | SIGHUP → SIGKILL delay. |
| `drain_output_on_close` | bool | `false` | Keep draining output after an explicit close. |
| `outgoing_limit_bytes` | integer, 32768–67108864 | `1048576` | Outgoing queue + in-flight limit. |
| `max_paste_bytes` | integer, 1–268435456 | `16777216` | Largest encoded paste, framing included. |

When consola supplies no custom environment, the child inherits consola's
environment; a custom environment replaces it wholesale. In both cases
`term` and `colorterm` replace any `TERM`/`COLORTERM` values.

`cursor` keys: `shape` (`block`, `underline`, `bar`; default `block`) and
`blink` (bool, default `true`).

## `font`

| Key | Type | Default |
| --- | --- | --- |
| `family` | string | `Inconsolata Condensed` |
| `size` | integer 1–512 | `14` |

## `colours`

The palette uses the 16 ANSI colour names; each is a `#rrggbb` string.

| Key | Type | Default |
| --- | --- | --- |
| `foreground` | colour | `#d8d8d8` |
| `background` | colour | `#181818` |
| `cursor` | colour | `#ffffff` |
| `palette` | map of name → colour | built-in |

`palette` names: `black`, `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`,
`white` (0–7) and `bright_black`, `bright_red`, `bright_green`,
`bright_yellow`, `bright_blue`, `bright_magenta`, `bright_cyan`,
`bright_white` (8–15).

## `profiles.<name>`

A profile may set any of the per-pane keys listed under [Precedence](#precedence).
`font`, `cursor` and `colours` are maps with the same shape as their top-level
(`font`, `colours`) or `general.cursor` counterparts; the remaining keys are
scalars or lists with the same shape as in `general`:

```yaml
profiles:
  sun:
    shell: /bin/zsh
    font:
      size: 16
    cursor:
      shape: bar
      blink: false
    colours:
      foreground: "#2b2b2b"
      background: "#fdf6e3"
      cursor: "#dc322f"
      palette:
        red: "#dc322f"
        bright_red: "#cb4b16"
```

## `shortcuts`

Shortcuts are canonical text such as `Ctrl+Shift+T`, `Alt+Left` or `Escape`.
Modifiers are `Ctrl`, `Shift` and `Alt`. Key names use FLTK/`X` names
(`PageUp`, `Left`, `Escape`, a single character, …).

| Key | Default |
| --- | --- |
| `new_tab` | `Ctrl+Shift+T` |
| `close_tab` | `Ctrl+Shift+X` |
| `rename_tab` | `Ctrl+Shift+R` |
| `select_profile` | `Ctrl+Shift+P` |
| `close_pane` | `Ctrl+Shift+W` |
| `split_horizontal` | `Ctrl+Shift+O` |
| `split_vertical` | `Ctrl+Shift+E` |
| `next_tab` | `Ctrl+PageDown` |
| `previous_tab` | `Ctrl+PageUp` |
| `focus_left` | `Alt+Left` |
| `focus_right` | `Alt+Right` |
| `focus_up` | `Alt+Up` |
| `focus_down` | `Alt+Down` |
| `resize_left` | `Ctrl+Shift+Left` |
| `resize_right` | `Ctrl+Shift+Right` |
| `resize_up` | `Ctrl+Shift+Up` |
| `resize_down` | `Ctrl+Shift+Down` |
| `cancel_paste` | `Escape` |
| `toggle_broadcast` | `Ctrl+Shift+B` |
| `toggle_zoom` | `Ctrl+Shift+Z` |
| `move_tab_left` | `Ctrl+Shift+PageUp` |
| `move_tab_right` | `Ctrl+Shift+PageDown` |
| `move_pane_left` | `Ctrl+Shift+Alt+Left` |
| `move_pane_right` | `Ctrl+Shift+Alt+Right` |
| `move_pane_up` | `Ctrl+Shift+Alt+Up` |
| `move_pane_down` | `Ctrl+Shift+Alt+Down` |

`cancel_paste` is parsed and reserved for the streaming-paste transport, which is
deferred until after the application is complete; it has no effect yet.

## `layouts`

Named saved layouts (DIP 5.6). A layout is data only: loading it does not create
tabs or processes (runtime reconstruction is Stage 6.5).

```yaml
layouts:
  work:
    active_tab: 0
    tabs:
      - root: 0
        focused: "p1"
        nodes:
          - split:
              orientation: horizontal # horizontal | vertical
              ratio: 0.5              # strictly between 0 and 1
              children: [1, 2]        # exactly two node indices
          - pane:
              id: "p1"                # unique within the tab
              profile: default        # must exist in profiles
              working_directory: /home/user
          - pane:
              id: "p2"
              profile: default
```

`nodes[root]` is the tree root. Every node is either a `split` (two child
indices and a ratio in `(0, 1)`) or a `pane` (a layout-local `id`, a `profile`
and an optional `working_directory`). The graph must be a tree: indices in
range, no cycles or shared children, no unreachable nodes, unique pane ids, and
`focused` must name a pane. `active_tab` indexes `tabs`. An invalid layout is
rejected without creating anything.

## Example

See [`resources/consola.yml`](../resources/consola.yml) for a complete file that
lists every key with its default value.
