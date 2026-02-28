# SingleThread Architecture

## Overview

SingleThread is a task-centric Wayland compositor and UI shell for Arch Linux.
The primary organizing concept is the **Task** (context), where windows are
grouped by the user's current activity rather than by workspace number.

## Component Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    SingleThread System                       │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              Compositor (singlethread)                │   │
│  │                                                       │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────────────────┐ │   │
│  │  │  wlroots  │ │ Task Sys │ │   Rules Engine       │ │   │
│  │  │  Backend  │ │          │ │   (window matching)   │ │   │
│  │  └──────────┘ └──────────┘ └──────────────────────┘ │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────────────────┐ │   │
│  │  │  Layout   │ │  Config  │ │   IPC Server         │ │   │
│  │  │  Engine   │ │  System  │ │   (JSON/Unix socket) │ │   │
│  │  └──────────┘ └──────────┘ └──────────────────────┘ │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────────────────┐ │   │
│  │  │ XWayland  │ │ Session  │ │   Keybinding         │ │   │
│  │  │ Support   │ │ Persist  │ │   Dispatch           │ │   │
│  │  └──────────┘ └──────────┘ └──────────────────────┘ │   │
│  └──────────────────────┬───────────────────────────────┘   │
│                         │ IPC Socket                         │
│  ┌──────────────────────┼───────────────────────────────┐   │
│  │          Shell Components (layer-shell)               │   │
│  │                                                       │   │
│  │  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌─────────┐ │   │
│  │  │  Panel   │ │ Launcher │ │ Switcher │ │ Notify  │ │   │
│  │  │stw-panel │ │stw-launc │ │stw-switc │ │stw-noti │ │   │
│  │  └──────────┘ └──────────┘ └──────────┘ └─────────┘ │   │
│  └───────────────────────────────────────────────────────┘   │
│                                                             │
│  ┌───────────────────────────────────────────────────────┐   │
│  │               CLI Tool (stwctl)                        │   │
│  │           JSON IPC client for scripting                │   │
│  └───────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## Core Concepts

### Task Hierarchy

```
Server
 └── Tasks (list)
      ├── Task "Development"
      │    ├── Workspace 1 (active)
      │    │    ├── View: terminal (tiled, master)
      │    │    └── View: editor (tiled, stack)
      │    └── Workspace 2
      │         └── View: browser (tiled)
      ├── Task "Communication"
      │    └── Workspace 1
      │         ├── View: slack (tiled)
      │         └── View: email (tiled)
      └── Global Views
           └── View: music player (floating, global)
```

### Window Lifecycle

1. New surface created (xdg_toplevel or XWayland)
2. View struct allocated, listeners attached
3. On map:
   a. Rules engine evaluates match criteria
   b. If rule matches → apply rule action (task/global/floating/size)
   c. If parent exists → inherit parent's task (TASK-4)
   d. Otherwise → assign to active task (TASK-3)
4. View is made visible/hidden based on task state
5. Layout engine arranges tiled views
6. On unmap/destroy: cleanup, re-layout

### Rule Precedence (RULE-3)

1. **Manual override** (user explicitly moved window)
2. **Specific window rule** (title regex match)
3. **Application rule** (app_id match)
4. **Default** (active task assignment)

## IPC Protocol

JSON messages over Unix domain socket with 4-byte length prefix.

**Request format:**
```json
{
  "command": "task/list"
}
```

**Response format:**
```json
{
  "success": true,
  "tasks": [...]
}
```

## Configuration

TOML format at `$XDG_CONFIG_HOME/singlethread/config.toml`.

Live reload supported for:
- Keybindings
- Rules
- Appearance settings
- Shell configuration

## Build System

- **Meson** build system
- Split packages: `singlethread` (compositor + CLI) and `singlethread-shell`
- Optional dependencies: XWayland (xcb), shell components (gtk4)

## Scene Graph Layers

Bottom to top:
1. `scene_background` - wallpaper/background
2. `scene_tiled` - tiled windows (task-scoped)
3. `scene_floating` - floating windows (task-scoped)
4. `scene_fullscreen` - fullscreen windows
5. `scene_global` - global windows (cross-task)
6. `scene_overlay` - layer-shell surfaces (panels, notifications)
