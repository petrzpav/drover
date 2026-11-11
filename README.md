# Drover

A floating indicator of your [herdr](https://herdr.dev) agents, on every
Omarchy workspace. The pill in the bottom-right corner counts the agents that
are working, asking you something, done, and idle. Click it and a panel opens
beside it: every agent of the local herdr session, the selected agent's screen,
and a box to message it, or keys to answer its question, without leaving the
workspace you're on.

![Drover with four agents](docs/screenshot.png)

## Use

Start **Drover** from the app launcher (`Super + Space`). Starting it again
opens or hides the panel of the one that is already running.

```sh
drover            # start, or open/hide the running one's panel
drover open       # start with the panel open, or open it
drover quit       # stop it
```

| Key / action        | Does                                                 |
| ------------------- | ---------------------------------------------------- |
| Click the pill      | Show or hide the panel                               |
| `↑` `↓`             | Pick an agent                                        |
| `Enter`             | Send the message; with an empty box, open the agent  |
| Double-click a row  | Open the agent in herdr                              |
| `Ctrl+O`            | Open the agent in herdr                              |
| `1`–`9`, `Enter`    | Answer an agent that asks you (and the key buttons)  |
| `Esc` / `Space`     | Hide the panel                                       |
| `?`                 | Show or hide the shortcuts                           |
| `Q`                 | Quit                                                 |

The panel opens with the message box focused, so `Space`, `?` and `Q` type
into it; they work as shortcuts once the box isn't focused (pick an agent with
the mouse). `Esc` always hides the panel.

Opening an agent focuses it in herdr (`herdr agent focus`) and brings the
herdr window forward, wherever it is.

## How it works

Drover is a wlr-layer-shell surface (via `layer-shell-qt`) on the top layer,
anchored to the bottom-right corner, so it floats over windows on every
workspace without window rules and hides under fullscreen windows. It takes
keyboard focus only while the panel is open.

It polls `herdr agent list` and `herdr workspace list` once a second (a few
milliseconds each) and reads the selected agent's screen with `herdr agent
read` while the panel is open. Only the local herdr session is shown; saved
SSH machines (`herdr --machine`) take seconds per call and are left out.

It follows the Omarchy theme accent, Hyprland's window rounding, and the
language picked under Setup > Language (English and Czech so far).

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
