# Drover

A floating indicator of your [herdr](https://herdr.dev) agents, on every
Omarchy workspace. The pill counts the agents that are working, asking you
something, done, and idle; drag it wherever you like, and it stays there.
Click it and a large panel opens beside it, like a chat: every agent of the
local herdr session on the left, the selected agent's screen on the right, and
a box to message it, or keys to answer its question, without leaving the
workspace you're on.

![Drover with four agents](docs/screenshot.png)

## Use

Start **Drover** from the app launcher (`Super + Space`). Starting it again
opens or hides the panel of the one that is already running.

```sh
drover            # start with the panel closed (autostart), or open/hide it
drover toggle     # open or hide the panel, starting drover if needed
drover open       # open the panel, starting drover if needed
drover quit       # stop it
```

A global shortcut is one line in `~/.config/hypr/bindings.lua`:

```lua
o.bind("SUPER + H", "Herdr agents", "drover toggle")
```

| Key / action        | Does                                                  |
| ------------------- | ----------------------------------------------------- |
| Click the pill      | Show or hide the panel                                |
| Drag the pill       | Move it; the panel opens on the side with room        |
| `Alt+↑` `Alt+↓`     | Pick an agent (plain `↑` `↓` outside the message box) |
| `Enter`             | Send the message; with an empty box, open the agent   |
| `Shift+Enter`       | New line in the message                               |
| Double-click a row  | Open the agent in herdr                               |
| `Ctrl+O`            | Open the agent in herdr                               |
| `1`–`9`, `Enter`    | Answer an agent that asks you (and the key buttons)   |
| `Esc`               | Hide the panel                                        |
| `?` / `Space` / `Q` | Help / hide / quit, outside the message box           |

The panel opens with the message box focused, so `Space`, `?`, `Q` and the
arrows type into it; they work as shortcuts once you pick an agent with the
mouse. `Esc` always hides the panel.

Opening an agent focuses it in herdr (`herdr agent focus`) and brings the
herdr window forward, wherever it is.

## How it works

Drover is a transparent wlr-layer-shell surface (via `layer-shell-qt`) on the
top layer, over the whole area the bar leaves free, so it floats over windows
on every workspace without window rules and hides under fullscreen windows.
Only the pill and the open panel take the pointer; everything else clicks
through. It takes keyboard focus only while the panel is open. The pill's
place is kept in `~/.local/state/drover/position`.

It polls `herdr agent list` and `herdr workspace list` once a second (a few
milliseconds each) and reads the selected agent's screen with `herdr agent
read` while the panel is open: its scrollback while it is idle, its visible
screen while it works (herdr can't scroll a busy agent's history). Only the local herdr session is shown; saved
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
bin/test       # Qt Test, offscreen
bin/install    # makepkg -fsi: installs the binary, launcher entry, and icon
```

Needs `qt6-base`, `qt6-declarative`, `qt6-wayland`, `layer-shell-qt`, and
`herdr`.

## License

MIT, see [LICENSE](LICENSE).
