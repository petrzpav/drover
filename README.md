<div align="center">

# 🐑 Drover

**Your herd of AI agents, always in sight.**

A tiny live herd of your [herdr](https://herdr.dev) agents, floating on every
[Omarchy](https://omarchy.org) workspace. You see who's working, who's done and
who needs you, and you answer them without leaving what you're doing.

[![Release](https://img.shields.io/github/v/release/petrzpav/drover?style=flat-square)](https://github.com/petrzpav/drover/releases/latest) [![License: MIT](https://img.shields.io/badge/license-MIT-blue?style=flat-square)](LICENSE) [![Omarchy](https://img.shields.io/badge/for-Omarchy-7aa2f7?style=flat-square)](https://omarchy.org) [![herdr](https://img.shields.io/badge/agents-herdr-9ece6a?style=flat-square)](https://herdr.dev)

![Drover: a question answered on the stage, the panel, theme switching](docs/demo.gif)

</div>

## Why Drover

- **Always on top, on every workspace.** Each agent is a pixel sheep. Working
  sheep wander, a finished one jumps into the pen, and the one asking stands
  under a blinking `?`.
- **Answer questions where you are.** Claude's questions and permission prompts
  land on a small stage beside the herd. Press `1`–`9` and the agent carries on.
- **A real terminal, one click away.** Click the herd and the agent's herdr pane
  opens in a panel: Shift+Tab, Esc, Ctrl+C, menus and colors work as they do in
  herdr. `Alt+↑` `Alt+↓` switch agents instantly, `Super+Alt+H` puts the panel
  over the whole screen, and `Ctrl+B` hides the agent list.
- **What the agent did, at a glance.** `Ctrl+Alt+B` shows the agent's
  conversation in points instead of its terminal: each of your prompts with
  what came of it, summed up by a model as it goes on. Click a point to open
  it, and send the agent a message from the field below.
- **An editor beside every agent.** `Alt+E` opens [Fresh](https://github.com/sinelaw/fresh)
  in the agent's folder, and `Ctrl+;` opens a shell below it.
- **New agents in a keystroke.** `Ctrl+P` lists your running agents to jump
  to, then the workspaces: pick or create one and your default agent starts
  there. An agent that ends takes its tab along,
  editor and shell included; the workspace's last tab stays.
- **Feels like part of Omarchy.** It uses the theme's colors, border and
  rounding and your terminal's font, and it follows theme switches live, Fresh
  included.

## Install

```sh
sudo pacman -U https://github.com/petrzpav/drover/releases/download/v0.9.0/drover-0.9.0-1-x86_64.pkg.tar.zst
```

It needs `herdr`; `fresh-editor` (AUR) adds the editor. To build from source,
run `git clone https://github.com/petrzpav/drover && cd drover && bin/install`.

Then start it at login, give it a key, and let Claude ask on its stage:

```lua
-- ~/.config/hypr/autostart.lua
o.launch_on_start("drover")
-- ~/.config/hypr/bindings.lua
o.bind("SUPER + H", "Herdr agents", "drover toggle")
o.bind("SUPER + ALT + H", "Herdr agents, full screen", "drover fullscreen")
```

```sh
drover hooks install    # Claude's questions on drover's stage
```

```ini
# ~/.config/drover/config — the sheep (the default is bars)
indicator = sheep
```

## The herd

| `indicator` | Looks like | Shows |
| ----------- | ---------- | ----- |
| `sheep` | ![sheep](docs/pill-sheep.gif) | A sheep per agent in its state's color: working sheep wander, done ones run into the pen to graze, the asking one stands under a blinking question mark, idle ones sleep. |
| `bars` | ![bars](docs/pill-bars.gif) | A bar per agent: working bars bounce, a finished bar shoots up and settles green, an asking one blinks red. |
| `dots` | ![dots](docs/pill-dots.gif) | How many agents are working, asking, done and idle. |

The border flashes green when an agent finishes. It flashes red when one asks,
and stays red while anyone is waiting for you. Drag the herd anywhere you like
and it stays there.

## Questions on the stage

![An agent's question on the stage](docs/stage.png)

When a Claude agent asks something, its sheep jumps the fence onto a card
beside the herd. Pick an option with `1`–`9`, or take the last row for your own
words. Permission prompts offer yes, yes and don't ask again, no, or no with
what to do instead. If an agent ends its turn with a plain question ("Shall I
push it?"), that comes on stage too, and drover types your answer into its
terminal. Only a yes-or-no question gets a "Yes" row; "Which one next?" takes
just your own words.

The stage never steals the keyboard. Click it or press `Super+H` to answer;
`Esc` leaves the question to the agent's terminal. While drover's panel is
open, nothing comes on stage: questions wait, the agent shows as asking in the
list, and they come once you close the panel. Picking the agent that asks gives
its question to its terminal.

<details>
<summary>How the stage hooks work</summary>

`drover hooks install` (or `uninstall`, `status`) adds three Claude Code hooks
to `~/.claude/settings.json` and leaves the rest of the file as it was. A copy of
the old file is kept as `settings.json.drover-backup`. Agents started afterwards
ask on the stage; restart running ones, or review the change under `/hooks` in
them.

The hook (`drover hook`) passes the question to drover over a local socket and
waits for your answer. When drover has nothing to do with the question, the hook
prints nothing and Claude shows its own dialog as before. That happens when:

- drover isn't running;
- the session isn't an agent in herdr (scripts, `claude -p`);
- you are already looking at that agent;
- the options carry previews, or Claude asks to approve a plan.

While drover holds a question, the herdr window shows the agent as working.
After `ask_hold` seconds without an answer, the question goes back to the
terminal. A permission prompt shows in the terminal at the same time, and you
can answer it in either place.

</details>

## The panel

![The panel with four agents](docs/screenshot.png)

Every agent of the local herdr session is on the left; drag one to another
place and the list, `Alt+↑` `Alt+↓`, `Ctrl+P` and the herd keep that order,
also after a restart. New agents come last. On the right is a real
terminal attached to the selected agent (`herdr terminal attach`), drawn by
drover itself on libvterm with no output buffering. The pane takes the panel's
size while it's open and gets herdr's size back when you close it.

| Key | Does |
| --- | ---- |
| `Super+H` | Show or hide the panel; with a question on stage, give it the keyboard |
| `Super+Alt+H` | Put the panel over the whole screen, and back |
| `Ctrl+B` | In the agent view: show or hide the agent list. Its button stays in the header, red when another agent asks |
| `Ctrl+Alt+B` | Switch between the agent's terminal and its conversation in points |
| `Alt+↑` `Alt+↓` | Pick an agent |
| `Alt+E` | Switch between the agent and its editor |
| `Ctrl+;` (editor) | Show or hide a shell below the editor |
| `Ctrl+P` | Go to a running agent, or pick a workspace and start one there; `Ctrl+O` picks a new folder, `Ctrl+D` twice closes a workspace |
| `Ctrl+Shift+O` | Open the agent in herdr |
| `Shift`+drag, `Ctrl+Shift+C` `V` | Select, copy, paste |
| `F1` | All shortcuts |

### The conversation in points

![The conversation in points](docs/overview.png)

`Ctrl+Alt+B`, or **overview** in the header's switch, shows the selected
Claude agent's conversation in points in place of its terminal, which stays
attached beneath. It is read from the transcript in `~/.claude/projects`, only
its end at first, then what is appended, and only while it shows; drover
remembers the view, and `Alt+E` comes back to it from the editor.

On the right, where the panel has room, an outline lists the turns like a
document's: each one's time, prompt and headline, the one in view marked and
the one at work with a bobbing robot; a click goes to a turn, and its button
hides it. `PgUp` and `PgDn` page the conversation, also from the message
field; `Ctrl+Home` and `Ctrl+End` go to its start and end. When Claude
parks a conversation as a job in the background, the overview follows it to
its new transcript.

Each of your prompts shows what came of it. A model sums up each turn as it
goes on: `claude -p` with Sonnet, one call at a time, on your Claude account
(about 5 seconds and 4,000 tokens a call). The summary is one to three short
points: a headline, where the turn stands, in bold, then only what else you
must know, quieter. It is rewritten as the turn goes on (four more points and
a minute later, or the turn's end), never added to, and kept in
`~/.cache/drover/summaries`. Under it shows only the newest point, what goes on
now. Older turns show just their prompt and headline; a click on the prompt
opens one. A short turn needs no summary, and an older one gets it when you
scroll to it. Once the agent stops, the newest turn's last reply shows in
full, as what it found, asks or offers is usually there. A question it waits
for, a command its last words give you to run (with a button that copies it),
and your own
messages still waiting in its queue are always in sight, and so are its
subagents and commands running in the background, until they end. A new summary waits
while the pointer is over the list and its turn in view, so nothing moves
under the pointer. When a picture explains a turn better than words (a
checklist of what works, a flow of stages, a tree of files, numbers before and
after, a little chart), the model draws one under a point, in plain text of at
most 10 lines of 64 characters, colored where it matters. After a failed call
or a usage limit, summaries rest for a while; `summary = off` in the config
turns them off.

The points themselves: commands in a row become one line, with the number of
the others after it. A failed or refused command gets a red line of its own,
and commits, pushes, releases and deploys a green one. Files edited with
Claude's Edit and Write tools are listed too; changes made by shell commands
show as those commands' descriptions. The agent's questions with your
answers, what you typed while it worked, what it said and Claude's recaps are
there as well. Reading and searching are left out.

While the agent works, its robot bobs on the point it is at, or on
"Thinking…" when no tool runs, so you see at a glance whether it is at work;
while the model sums a turn up, it bobs on the first point being summed up.

Click a summary point to open the points it sums up, and a point to open it
where it is: the agent's words in full (as Markdown, its pictures as links,
nothing fetched), the error, every command of a run. Never all at once: a long
turn shows its newest points, and "… N earlier points" twenty more a click.
The list stays at the bottom as the agent goes on, unless you scrolled up;
while the pointer is over it, it waits, so nothing moves under the pointer.

The field below sends the agent a message (`herdr agent prompt`): `Enter`
sends it, `Shift+Enter` starts a new line. A picture pasted with `Ctrl+V` is
saved to `~/.cache/drover/pasted` and goes as its path, which the agent opens. An agent at work takes it as it
goes on, as when typed; not over a draft in its own prompt, and not while it
asks you something: then the field says so, and a click on it takes you to the
terminal. Each agent keeps its own draft.

Any other key goes to the agent, as in herdr. In the agent view, `Ctrl+B` is
drover's, not Claude's (run in the background); the editor and the shell keep
theirs. Mouse wheel and clicks go to herdr too, which scrolls the agent's
history. drover remembers the full screen, the hidden list and the view
(terminal or points), also after a restart.

## Configuration

`~/.config/drover/config`, read at start:

```ini
# sheep, bars (the default) or dots
indicator = sheep
# what Ctrl+P starts: claude (the default), codex, …
agent = claude
# the editor beside an agent: fresh (the default), micro, …
editor = fresh
# how the sheep comes on stage: fence (the default) or spotlight
stage = fence
# end-of-turn questions on the stage too: yes (the default) or no
ask_replies = yes
# seconds before an unanswered question goes back to the terminal; 0 waits for good
ask_hold = 180
# the model that sums up turns in the overview: sonnet (the default), haiku, … or off
summary = sonnet
# close workspaces with a herdr plugin's action instead of herdr workspace close
# close_action = petrzpav.persistent-spaces.delete
```

```sh
drover               # start (autostart), or open/hide the running one
drover toggle|open   # open or hide the panel, starting drover if needed
drover fullscreen    # the panel over the whole screen, and back
drover pick          # open the panel with the workspace picker
drover quit
```

<details>
<summary>Workspace picker details</summary>

`Ctrl+P` lists the running agents first, with the highlight on the one you
were at before, so `Ctrl+P` `Enter` switches back and forth; typing narrows
them by title or workspace. Below them are the workspaces: typing a
workspace's name puts the highlight on it, and Enter opens a new tab there, in
the folder it already works in, and starts the default agent. A name no workspace has yet adds a
**Create** row. Enter opens the desktop's folder picker (the xdg portal), unless
the text is already a folder under your home (`~/Work/orchard`). A folder that
doesn't exist yet, inside one that does, shows **new folder** and is made first.
`Ctrl+D`, or the trash button, marks a workspace; a second press runs
`herdr workspace close`, or the plugin action set as `close_action`.

Hyprland bindings that take `Ctrl+P` globally can pass it on while drover's
panel has the keyboard, which drover marks with `$XDG_RUNTIME_DIR/drover.focus`:

```lua
local focus = io.open((os.getenv("XDG_RUNTIME_DIR") or "") .. "/drover.focus")
if focus then focus:close(); hl.exec_cmd("drover pick") return end
```

</details>

<details>
<summary>How it works</summary>

Drover is a transparent wlr-layer-shell surface (`layer-shell-qt`) on the top
layer, so it floats over windows on every workspace without window rules and
hides under fullscreen windows. Only the herd and the open panel take the
pointer; everything else clicks through. It takes the keyboard only while the
panel is open, or the stage once you give it the keyboard.

It lists agents whenever herdr's socket reports a change, so the herd follows
within milliseconds. It also polls, every 5 s while events flow and once a
second otherwise. Only the local herdr session is shown, because saved SSH
machines take seconds per call.

The look comes from Omarchy: the theme's `colors.toml`, the Hyprland active
border, Hyprland's rounding and outer gaps (the full-screen panel stands
where a window would), and the font and size of your terminal (Ghostty,
Alacritty, kitty or foot). Theme switches and the language (English, Czech)
apply live. drover also writes a Fresh theme from the Omarchy colors to
`~/.config/fresh/themes/omarchy.json`; use it with `{ "theme": "omarchy.json" }`
in Fresh's `config.json`.

</details>

## Build

```sh
bin/build        # build/drover
bin/test         # Qt Test, offscreen: the backend, and the real QML against a fake herdr
bin/install      # makepkg -fsi: binary, launcher entry, icon
bin/screenshots  # redraws docs/ against a made-up herd (needs ffmpeg)
```

Needs `qt6-base`, `qt6-declarative`, `qt6-wayland`, `layer-shell-qt`, `libvterm`
and `herdr`.

## License

MIT, see [LICENSE](LICENSE).
