# SingleThread - Task-Centric Wayland Compositor

## Project Overview

SingleThread is a Wayland compositor + UI shell where **Tasks** (contexts) are the
primary organizing concept. Windows are grouped by activity, not just workspaces.

## Build

```bash
meson setup build
meson compile -C build
```

Dependencies: wayland, wayland-protocols, wlroots (0.18+), libinput, xkbcommon,
pixman, json-c, gtk4, gtk4-layer-shell.

## Project Structure

- `src/compositor/` - Core Wayland compositor (C, wlroots)
  - `server.{h,c}` - Server initialization, lifecycle, focus management
  - `view.{h,c}` - Window (view) management, task assignment
  - `task.{h,c}` - Task system (create, switch, activate, workspaces)
  - `output.{h,c}` - Monitor/output handling
  - `input.{h,c}` - Keyboard and pointer input
  - `layout.{h,c}` - Tiling layouts (master-stack, BSP)
  - `rules.{h,c}` - Window rule matching engine
  - `stw_config.{h,c}` - TOML config parsing, keybindings
  - `keybind.{h,c}` - Keybinding dispatch and action execution
  - `ipc.{h,c}` - JSON IPC over Unix socket
  - `session.{h,c}` - Session persistence (JSON)
  - `xwayland.{h,c}` - XWayland support
- `src/cli/stwctl.c` - CLI tool for IPC
- `src/shell/` - GTK4 layer-shell UI components
  - `panel/` - Top bar (task indicators, clock, tray)
  - `launcher/` - App launcher (.desktop search)
  - `switcher/` - Task switcher overlay
  - `notifications/` - Notification popups
- `config/` - Example configuration
- `docs/` - Documentation
- `packaging/arch/` - Arch Linux PKGBUILD

## Key Design Decisions

- wlroots 0.18 scene-graph API for rendering
- Tasks own views; views track their task assignment
- Rules have precedence: manual > specific rule > app rule > default
- IPC uses JSON over length-prefixed Unix socket
- Shell components are separate processes using layer-shell
- Config is TOML with live reload via inotify
