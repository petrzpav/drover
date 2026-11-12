# Drover

A floating indicator of your [herdr](https://herdr.dev) agents, on every
Omarchy workspace. The pill is a small live herd that shows what every agent
is doing and lights up when one finishes or asks you something; drag it
wherever you like, and it stays there. Click it and a large panel opens beside it: every agent of the local herdr
session on the left, and on the right a real terminal attached to the selected
agent, where you work with it exactly as in herdr (Shift+Tab, Esc, Ctrl+C, its
menus and colors), without leaving the workspace you're on.

![Drover with four agents](docs/screenshot.png)

### The pill

Three looks, set in `~/.config/drover/config` (bars when there is none);
drover reads it when it starts:

```ini
indicator = sheep
```

| `indicator` | Looks like | Shows |
| ----------- | ---------- | ----- |
| `bars` | ![bars](docs/pill-bars.gif) | A bar per agent: working bars bounce, a bar that finishes shoots up and settles green, one that asks you blinks red, idle ones lie low. Beside them the state that matters most: how many ask you, else work, else are done. |
| `sheep` | ![sheep](docs/pill-sheep.gif) | A pixel sheep per agent, in its state's color: working sheep wander the pasture, one that finishes runs and jumps into the pen to graze, one that asks you stands under a blinking question mark, idle ones sleep. |
| `dots` | ![dots](docs/pill-dots.gif) | How many agents work, ask you, are done, and are idle. |

When an agent finishes, the pill's border flashes green (bars and sheep); when
one asks you, it flashes red and stays red while anyone waits for you.

## Use

Start **Drover** from the app launcher (`Super + Space`). Starting it again
opens or hides the panel of the one that is already running.

```sh
drover            # start with the panel closed (autostart), or open/hide it
drover toggle     # open or hide the panel, starting drover if needed
drover open       # open the panel, starting drover if needed
drover pick       # open the panel with the workspace picker
drover quit       # stop it
```

A global shortcut is one line in `~/.config/hypr/bindings.lua`:

```lua
o.bind("SUPER + H", "Herdr agents", "drover toggle")
```

| Key / action        | Does                                                       |
| ------------------- | ---------------------------------------------------------- |
| Click the pill      | Show or hide the panel                                     |
| Drag the pill       | Move it; the panel opens on the side with room             |
| Any key             | Goes to the agent's terminal, as in herdr                  |
| Wheel, clicks       | Go to herdr, which scrolls the agent's history         |
| `Shift`+drag        | Select text                                                |
| `Ctrl+Shift+C` `Ctrl+Shift+V` | Copy the selection, paste (bracketed)            |
| `Alt+↑` `Alt+↓`     | Pick an agent                                              |
| `Ctrl+P`            | Pick a workspace and start the default agent there         |
| `Ctrl+O` (picker)   | Pick the folder for a new workspace                        |
| `Ctrl+D` (picker)   | Close the highlighted workspace; press again to confirm    |
| `Ctrl+Shift+O`      | Open the agent in herdr (or double-click its row)          |
| `Super+H`           | Show or hide the panel (with the binding below)            |
| `Esc`               | Close the picker or help; outside the terminal, hide the panel |
| `F1`                | Show or hide the shortcuts                                 |
| `Ctrl+Q`            | Quit                                                       |

The terminal is `herdr terminal attach` on the agent's pane, so it shows and
takes exactly what herdr's own window does. While the panel is open the pane
takes the panel's size; closing the panel detaches, and the pane gets herdr's
size back. Opening an agent focuses it in herdr (`herdr agent focus`) and
brings the herdr window forward, wherever it is.

### The default agent

`Ctrl+P` opens a new tab in the chosen workspace, in the folder it already
works in, and starts the agent kind set in `~/.config/drover/config`
(`claude` when there is none):

```ini
agent = codex
```

A name no workspace has yet adds a **Create** row. Enter (or `Ctrl+O`, or the
row's folder button) opens the desktop's folder picker (the xdg portal, flea on
Omarchy); the panel steps aside meanwhile. Once you pick a folder, drover
creates the workspace there (`herdr workspace create`) and starts the agent in
its first pane; cancelling brings the picker back. Text that is already a
folder, relative to your home (`~/Work/orchard`, `Work/orchard`), skips the
picker: Enter creates the workspace there, named after the folder. A path to a
folder that doesn't exist yet, inside one that does (`Work/orchard` with
`~/Work` there), shows **new folder** on the row: Enter makes it first.

`Ctrl+D`, or the trash button on a row, marks a workspace for closing; a second
`Ctrl+D` (or Enter, or click) runs `herdr workspace close`, which ends every
pane and agent in it. Esc, typing, or moving to another row takes the mark back.

If a herdr plugin brings closed workspaces back unless its own action closed
them, name that action in the config; drover then runs it for the workspace
(over herdr's socket, `plugin.action.invoke`) instead of `herdr workspace close`:

```ini
close_action = petrzpav.persistent-spaces.delete
```

Hyprland setups that bind `Ctrl+P` globally swallow the key before drover
sees it, since a layer surface with the keyboard is never Hyprland's active
window. Such a binding can hand the key on while drover's panel has the
keyboard, which drover marks with `$XDG_RUNTIME_DIR/drover.focus`:

```lua
local focus = io.open((os.getenv("XDG_RUNTIME_DIR") or "") .. "/drover.focus")
if focus then focus:close(); hl.exec_cmd("drover pick") return end
```

## How it works

Drover is a transparent wlr-layer-shell surface (via `layer-shell-qt`) on the
top layer, over the whole area the bar leaves free, so it floats over windows
on every workspace without window rules and hides under fullscreen windows.
Only the pill and the open panel take the pointer; everything else clicks
through. It takes keyboard focus only while the panel is open. The pill's
place is kept in `~/.local/state/drover/position`.

It polls `herdr agent list` and `herdr workspace list` once a second (a few
milliseconds each). A failed poll keeps the last list and says it is out of
date; if the selected agent's pane closes, nothing is selected until you pick
another, so the terminal never attaches to an agent you didn't choose. The
terminal is drover's own: [libvterm](https://www.leonerd.org.uk/code/libvterm/)
(the engine of Neovim's terminal) parses the output, and drover draws it on
the next frame with no buffering in between, in the theme's 16 colors, so
typing is as quick as in a standalone terminal. Only the local herdr session is shown; saved
SSH machines (`herdr --machine`) take seconds per call and are left out.

It looks like the Omarchy shell: the theme's colors from `colors.toml`, the
Hyprland active border (`hyprland_active_border`) around the panel and pill,
Hyprland's window rounding, and your terminal's font and size (read from the
config of the first terminal in `~/.config/xdg-terminals.list`: Ghostty,
Alacritty, kitty, or foot), so it reads like the terminal the agents run in. It follows theme
switches and the language picked under Setup > Language (English and Czech so
far) live.

To start it at login, add to `~/.config/hypr/autostart.lua`:

```lua
o.launch_on_start("drover")
```

## Build

```sh
bin/build      # build/drover
bin/test       # Qt Test, offscreen: the backend, and the real QML driven against a fake herdr
bin/install    # makepkg -fsi: installs the binary, launcher entry, and icon
bin/screenshots  # redraws the pictures in docs/ against a made-up herd (needs ffmpeg)
```

Needs `qt6-base`, `qt6-declarative`, `qt6-wayland`, `layer-shell-qt`,
`libvterm`, and `herdr`.

## License

MIT, see [LICENSE](LICENSE).
