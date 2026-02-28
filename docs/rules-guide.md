# SingleThread Rules Guide

## Overview

The rules engine automatically manages window behavior based on match criteria.
Rules are evaluated when a window first maps (appears) and determine:

- Which task a window belongs to
- Whether it's global (visible across all tasks)
- Whether it floats or tiles
- Its initial size and position

## Rule Structure

Rules are defined as `[[rule]]` entries in your config file:

```toml
[[rule]]
app_id = "firefox"
task = "Browser"
```

Each rule has **match criteria** and **actions**.

## Match Criteria

### `app_id` - Wayland App ID

Matches the Wayland `app_id` property. This is the most reliable identifier
for native Wayland applications.

```toml
[[rule]]
app_id = "firefox"
```

To find an app's ID, use:
```bash
stwctl window list
```

### `class` - XWayland WM_CLASS

Matches the X11 `WM_CLASS` property for XWayland applications.

```toml
[[rule]]
class = "Steam"
```

### `title` - Window Title (Regex)

Matches the window title using a regular expression. This is fragile since
titles change, but useful for specific cases.

```toml
[[rule]]
title = "(?i)file.*chooser"
floating = true
```

The regex is case-insensitive by default.

## Actions

### `task` - Assign to Named Task

Assigns matching windows to a specific task. If the task doesn't exist,
it will be created automatically.

```toml
[[rule]]
app_id = "firefox"
task = "Browser"
```

### `global` - Mark as Global

Makes the window visible across all tasks.

```toml
[[rule]]
app_id = "spotify"
global = true
```

### `floating` - Float or Tile

Forces the window to float (or tile if set to `false`).

```toml
[[rule]]
app_id = "pavucontrol"
floating = true
```

### `size` - Initial Size

Sets the initial size for floating windows.

```toml
[[rule]]
app_id = "keepassxc"
floating = true
size = { width = 800, height = 600 }
```

### `workspace` - Assign to Workspace

Places the window on a specific workspace within its task (0-indexed).

```toml
[[rule]]
app_id = "firefox"
task = "Browser"
workspace = 0
```

## Rule Precedence

When multiple rules could match, the following precedence applies:

1. **Manual override** - If the user has explicitly moved a window to a task
   (e.g., via `Super+Shift+N`), no rule can override it.

2. **First matching rule** - Rules are evaluated in config file order.
   The first rule that matches wins.

3. **Default behavior** - If no rule matches, the window is assigned to
   the currently active task.

## Common Examples

### Development Setup

```toml
# Code editors to Development task
[[rule]]
app_id = "code"
task = "Development"

[[rule]]
app_id = "neovide"
task = "Development"

# Terminals to Development task
[[rule]]
app_id = "foot"
task = "Development"
```

### Communication

```toml
[[rule]]
app_id = "slack"
task = "Communication"

[[rule]]
app_id = "thunderbird"
task = "Communication"

[[rule]]
app_id = "discord"
task = "Communication"
```

### Global Windows

```toml
# Music always visible
[[rule]]
app_id = "spotify"
global = true

# System monitor
[[rule]]
app_id = "gnome-system-monitor"
global = true
floating = true
```

### Floating Utilities

```toml
# Password manager
[[rule]]
app_id = "keepassxc"
floating = true
size = { width = 800, height = 600 }

# Calculator
[[rule]]
app_id = "gnome-calculator"
floating = true

# Color picker
[[rule]]
title = "(?i)color.*picker"
floating = true

# File dialogs (common pattern)
[[rule]]
title = "(?i)(open|save|choose|select).*file"
floating = true
```

### XWayland Applications

```toml
# Steam
[[rule]]
class = "Steam"
task = "Gaming"
floating = true

# Wine applications
[[rule]]
class = "Wine"
floating = true
```

## Debugging Rules

Enable debug logging to see rule matching:

```bash
singlethread -d
```

Look for log lines like:
```
[DEBUG] Rule matched for 'firefox' (title='Mozilla Firefox')
[DEBUG] View 'firefox' assigned to task 'Browser' (source=1)
```

You can also trace rules for the currently focused window via IPC
(when implemented).
