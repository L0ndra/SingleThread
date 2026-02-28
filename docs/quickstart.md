# SingleThread Quick Start Guide

## Installation

### From source (Arch Linux)

```bash
# Install dependencies
sudo pacman -S wayland wayland-protocols wlroots libinput \
  xkbcommon pixman json-c libxcb xcb-util-errors \
  gtk4 gtk4-layer-shell meson ninja

# Clone and build
git clone https://github.com/singlethread/singlethread.git
cd singlethread
meson setup build
meson compile -C build
sudo meson install -C build
```

### Using the PKGBUILD

```bash
cd packaging/arch
makepkg -si
```

## First Run

### From a display manager

SingleThread installs a session entry at `/usr/share/wayland-sessions/singlethread.desktop`.
Select "SingleThread" from your display manager's session list.

### From TTY

```bash
# Start SingleThread directly
singlethread
```

### With custom config

```bash
singlethread -c ~/.config/singlethread/config.toml
```

## Configuration

Copy the example config:

```bash
mkdir -p ~/.config/singlethread
cp /usr/share/doc/singlethread/singlethread.toml.example \
   ~/.config/singlethread/config.toml
```

Edit `~/.config/singlethread/config.toml` to customize.

## Essential Keybindings

Default keybindings (all use `Super` as the modifier):

### Task Management
| Key | Action |
|-----|--------|
| `Super+t` | Create new task |
| `Super+w` | Close current task |
| `Super+Tab` | Switch to next task |
| `Super+Shift+Tab` | Switch to previous task |
| `Super+1-9` | Switch to task by number |
| `Super+Shift+1-9` | Move window to task by number |

### Window Management
| Key | Action |
|-----|--------|
| `Super+q` | Close window |
| `Super+f` | Toggle fullscreen |
| `Super+Shift+f` | Toggle floating |
| `Super+g` | Toggle global (visible in all tasks) |
| `Super+h/j/k/l` | Focus left/down/up/right |
| `Super+Shift+h/j/k/l` | Swap window left/down/up/right |
| `Super+Return` | Zoom (swap with master) |

### Layout
| Key | Action |
|-----|--------|
| `Super+]` | Increase master ratio |
| `Super+[` | Decrease master ratio |
| `Super+,` | Increase master count |
| `Super+.` | Decrease master count |
| `Super+Space` | Cycle layout (master-stack → BSP → floating) |

### Shell
| Key | Action |
|-----|--------|
| `Super+d` | Open launcher |
| `` Super+` `` | Open task switcher |
| `Super+o` | Open overview |
| `Super+Shift+Return` | Open terminal (foot) |
| `Super+Shift+q` | Exit compositor |

## CLI Tool (stwctl)

```bash
# List tasks
stwctl task list

# Create a task
stwctl task create "Development"

# Switch to a task
stwctl task switch Development

# List windows
stwctl window list

# Move focused window to another task
stwctl window to-task 2

# Toggle focused window as global
stwctl window toggle-global

# Reload config
stwctl reload

# Launch an app
stwctl launch firefox
```

## Concepts

### Tasks
Tasks are the primary organizing concept. Think of them as activity contexts:
- "Development" - your editor, terminal, documentation
- "Communication" - email, chat, video calls
- "Research" - browsers, note-taking apps

Each task has its own set of windows, layout state, and optional workspaces.

### Global Windows
Some windows should be visible across all tasks. Mark a window as "global"
with `Super+g` and it will stay visible when you switch tasks. Common uses:
music players, system monitors, messengers.

### Rules
Window rules automatically assign windows to tasks or configure their behavior.
Add rules in your config file:

```toml
# Assign Firefox to a "Browser" task
[[rule]]
app_id = "firefox"
task = "Browser"

# Make Spotify global
[[rule]]
app_id = "spotify"
global = true

# Float password managers
[[rule]]
app_id = "keepassxc"
floating = true
size = { width = 800, height = 600 }
```

## Troubleshooting

Enable debug logging:
```bash
singlethread -d
```

Check logs:
```bash
journalctl --user -u singlethread
```

See [troubleshooting.md](troubleshooting.md) for more.
