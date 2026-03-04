# Testing & Development Guide

This guide covers how to build, test, and develop SingleThread on any platform,
including running it on macOS via a Linux VM.

## Table of Contents

- [Quick Start (Native Linux)](#quick-start-native-linux)
- [macOS: Running via UTM + NixOS VM](#macos-running-via-utm--nixos-vm)
- [macOS: Running via Lima (Headless Build)](#macos-running-via-lima-headless-build)
- [NixOS: Declarative Installation](#nixos-declarative-installation)
- [Development Workflow](#development-workflow)
- [Testing the ADHD Features](#testing-the-adhd-features)
- [Debugging](#debugging)

---

## Quick Start (Native Linux)

### Arch Linux

```bash
# Install dependencies
sudo pacman -S wayland wayland-protocols wlroots libinput \
  xkbcommon pixman json-c libxcb xcb-util-errors \
  gtk4 gtk4-layer-shell meson ninja

# Build
git clone https://github.com/L0ndra/SingleThread.git
cd SingleThread
meson setup build
meson compile -C build

# Run from TTY (not inside another compositor)
./build/src/compositor/singlethread
```

### Fedora

```bash
sudo dnf install wayland-devel wayland-protocols-devel \
  wlroots-devel libinput-devel libxkbcommon-devel \
  pixman-devel json-c-devel gtk4-devel gtk4-layer-shell-devel \
  meson ninja-build gcc xcb-util-errors-devel
```

### Ubuntu / Debian (24.04+)

```bash
sudo apt install libwayland-dev wayland-protocols \
  libwlroots-dev libinput-dev libxkbcommon-dev \
  libpixman-1-dev libjson-c-dev libgtk-4-dev \
  libgtk4-layer-shell-dev meson ninja-build \
  libxcb1-dev libxcb-errors-dev
```

### NixOS / Nix

```bash
# Enter dev shell (installs everything)
nix develop

# Or build directly
nix build
```

---

## macOS: Running via UTM + NixOS VM

This is the recommended approach for Mac users. UTM provides near-native
performance on Apple Silicon with GPU passthrough for smooth Wayland rendering.

### Step 1: Install UTM

```bash
brew install --cask utm
```

Or download from https://mac.getutm.app (free from GitHub, paid on App Store).

### Step 2: Download NixOS ISO

Get the **NixOS minimal ISO** for your architecture:

- Apple Silicon (M1/M2/M3/M4): `nixos-minimal-*-aarch64-linux.iso`
- Intel Mac: `nixos-minimal-*-x86_64-linux.iso`

Download from https://nixos.org/download/

### Step 3: Create the VM

1. Open UTM and click **Create a New Virtual Machine**
2. Select **Virtualize** (not Emulate)
3. Choose **Linux**
4. Configure:
   - **Boot ISO**: Select the NixOS ISO you downloaded
   - **Memory**: 8192 MB (8 GB recommended, 4 GB minimum)
   - **CPU**: 4+ cores
   - **Storage**: 64 GB (dynamic)
   - **Display**: virtio-gpu-gl (for GPU acceleration)
   - **Network**: Shared (NAT)
5. Under **Display** settings:
   - Resolution: Match your monitor (e.g., 2560x1440)
   - Check "Retina Mode" if on a HiDPI display
6. Click **Save**, then **Start**

### Step 4: Install NixOS in the VM

Boot from the ISO and run:

```bash
# Become root
sudo -i

# Partition disk (assuming /dev/vda)
parted /dev/vda -- mklabel gpt
parted /dev/vda -- mkpart ESP fat32 1MB 512MB
parted /dev/vda -- set 1 esp on
parted /dev/vda -- mkpart primary 512MB 100%

# Format
mkfs.fat -F 32 -n BOOT /dev/vda1
mkfs.ext4 -L nixos /dev/vda2

# Mount
mount /dev/disk/by-label/nixos /mnt
mkdir -p /mnt/boot
mount /dev/disk/by-label/BOOT /mnt/boot

# Generate initial config
nixos-generate-config --root /mnt
```

### Step 5: Configure NixOS with SingleThread

Edit `/mnt/etc/nixos/configuration.nix`:

```nix
{ config, pkgs, ... }:

{
  imports = [ ./hardware-configuration.nix ];

  # Boot
  boot.loader.systemd-boot.enable = true;
  boot.loader.efi.canTouchEfiVariables = true;

  # Networking
  networking.hostName = "singlethread-dev";
  networking.networkmanager.enable = true;

  # User account
  users.users.dev = {
    isNormalUser = true;
    extraGroups = [ "wheel" "video" "input" "seat" ];
    initialPassword = "changeme";
  };

  # Enable seat management (required for Wayland compositors)
  services.seatd.enable = true;
  security.polkit.enable = true;
  hardware.graphics.enable = true;

  # Development tools
  environment.systemPackages = with pkgs; [
    git
    neovim
    foot              # Wayland terminal
    firefox
    meson
    ninja
    pkg-config
    gcc
    gdb
    valgrind

    # SingleThread build dependencies
    wayland
    wayland-protocols
    wayland-scanner
    wlroots_0_18
    libinput
    xkbcommon
    pixman
    json_c
    gtk4
    gtk4-layer-shell
    libxcb
    xcb-util-errors

    # Wayland utilities
    wl-clipboard
    grim
    slurp
    wlr-randr
  ];

  # Fonts for the UI
  fonts.packages = with pkgs; [
    inter
    jetbrains-mono
    noto-fonts
    noto-fonts-emoji
    font-awesome
  ];
  fonts.fontconfig.defaultFonts = {
    sansSerif = [ "Inter" "Noto Sans" ];
    monospace = [ "JetBrains Mono" ];
  };

  # Portal support
  xdg.portal = {
    enable = true;
    wlr.enable = true;
    extraPortals = [ pkgs.xdg-desktop-portal-gtk ];
  };

  # Wayland environment
  environment.sessionVariables = {
    XDG_SESSION_TYPE = "wayland";
    MOZ_ENABLE_WAYLAND = "1";
    QT_QPA_PLATFORM = "wayland";
  };

  # Auto-login to TTY (optional, reduces friction)
  services.getty.autologinUser = "dev";

  system.stateVersion = "24.11";
}
```

Install and reboot:

```bash
nixos-install
reboot
```

### Step 6: Build and Run SingleThread

After rebooting into NixOS:

```bash
# Login as dev
# Clone and build
git clone https://github.com/L0ndra/SingleThread.git
cd SingleThread
meson setup build
meson compile -C build

# Copy example config
mkdir -p ~/.config/singlethread
cp config/singlethread.toml.example ~/.config/singlethread/config.toml

# Run SingleThread from TTY (Ctrl+Alt+F2 if needed)
./build/src/compositor/singlethread
```

### UTM Tips

- **Shared folders**: Use UTM's directory sharing to edit code on macOS and
  build inside the VM. In VM settings, add a shared directory pointing to your
  code, then inside NixOS:
  ```bash
  sudo mount -t virtiofs share /mnt/shared
  ```
- **Clipboard**: Install `spice-vdagent` in the VM for clipboard sharing
- **Resolution**: Use `wlr-randr` inside SingleThread to adjust output resolution
- **Performance**: Enable "GPU Acceleration" in UTM display settings

---

## macOS: Running via Lima (Headless Build)

If you only need to compile-check (no graphical testing), Lima is lighter:

```bash
# Install Lima
brew install lima

# Create a VM with Ubuntu
limactl create --name=stw --vm-type=vz --mount-writable default
limactl start stw
limactl shell stw

# Inside the VM
sudo apt update && sudo apt install -y \
  libwayland-dev wayland-protocols libwlroots-dev libinput-dev \
  libxkbcommon-dev libpixman-1-dev libjson-c-dev libgtk-4-dev \
  libgtk4-layer-shell-dev meson ninja-build libxcb1-dev \
  libxcb-errors-dev gcc pkg-config

# Build (your macOS home directory is mounted at ~)
cd ~/SingleThread
meson setup build
meson compile -C build
```

This verifies the code compiles but you won't be able to run the compositor
(no display). Useful for CI-style validation.

---

## NixOS: Declarative Installation

If you're already running NixOS, use the flake directly:

### Using the Flake

Add to your `flake.nix`:

```nix
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    singlethread.url = "github:L0ndra/SingleThread";
  };

  outputs = { nixpkgs, singlethread, ... }: {
    nixosConfigurations.myhost = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";  # or "aarch64-linux"
      modules = [
        ./hardware-configuration.nix
        singlethread.nixosModules.default
        {
          programs.singlethread = {
            enable = true;
            xwayland = true;
            focusMode = {
              breakInterval = 25;
              breakDuration = 5;
              suppressNotifications = true;
            };
          };
        }
      ];
    };
  };
}
```

Then:

```bash
sudo nixos-rebuild switch --flake .#myhost
```

### Using the Overlay (for Home Manager etc.)

```nix
{
  nixpkgs.overlays = [ singlethread.overlays.default ];
  # Now `pkgs.singlethread` is available
}
```

---

## Development Workflow

### Build commands

```bash
# Initial setup
meson setup build

# Compile (incremental)
meson compile -C build

# Compile with debug symbols
meson setup build --buildtype=debug --reconfigure
meson compile -C build

# Clean rebuild
rm -rf build && meson setup build && meson compile -C build
```

### Running in a nested Wayland session

You can run SingleThread inside another Wayland compositor for testing
(useful during development so you don't lose your session):

```bash
# Set a custom socket name so it doesn't conflict
WLR_BACKENDS=wayland ./build/src/compositor/singlethread

# Or inside X11
WLR_BACKENDS=x11 ./build/src/compositor/singlethread
```

This opens SingleThread in a window. Focus mode, task switching, IPC -
everything works. The shell components (panel, launcher) will connect to
the nested session's socket.

### Testing the IPC

With SingleThread running (even nested), open a terminal inside it:

```bash
# List tasks
./build/src/cli/stwctl task list

# Create a task
./build/src/cli/stwctl task create "Testing"

# Switch tasks
./build/src/cli/stwctl task switch Testing

# Direct JSON IPC (for testing new commands)
echo '{"command": "task/list"}' | \
  socat - UNIX-CONNECT:$SINGLETHREAD_SOCKET
```

### Testing shell components standalone

The panel, launcher, switcher, and quicknote are separate GTK4 processes.
You can test them outside the compositor (they'll show fallback data when
IPC is unavailable):

```bash
# Test panel rendering
./build/src/shell/panel/stw-panel

# Test quick note overlay
./build/src/shell/quicknote/stw-quicknote

# Test launcher
./build/src/shell/launcher/stw-launcher
```

Note: Layer-shell features (anchoring, exclusive zones) only work under a
Wayland compositor that supports `wlr-layer-shell`.

### Live config reload

Edit `~/.config/singlethread/config.toml` while the compositor is running:

```bash
# Reload via keybind
# Press Super+Shift+r (if bound to compositor:reload)

# Or via CLI
./build/src/cli/stwctl reload
```

---

## Testing the ADHD Features

### Focus Mode

```bash
# Toggle focus mode (keyboard)
# Press Super+F12

# Start a 25-minute Pomodoro
# Press Super+Shift+F12

# Via IPC
./build/src/cli/stwctl -j '{"command": "focus/start", "minutes": 25}'

# Check status
./build/src/cli/stwctl -j '{"command": "get/focus-mode"}'
# Returns: {"active": true, "on_break": false, "elapsed_minutes": 3, ...}

# Stop focus mode
./build/src/cli/stwctl -j '{"command": "focus/stop"}'
```

When focus mode is active, you should see:
- Amber "Focus 3m" indicator in the panel (right side)
- Inactive windows dimmed more heavily
- Notifications suppressed
- After 25 minutes: red "Break!" indicator appears

### Task Timer

The timer runs automatically. Switch between tasks and watch:
- Green time badge on the active task pill (e.g., "1h 23m")
- Center timer display next to the window title

### Per-Task Accent Colors

Create several tasks and observe the colored dots on task pills:

```bash
./build/src/cli/stwctl task create "Code"      # blue dot
./build/src/cli/stwctl task create "Research"   # purple dot
./build/src/cli/stwctl task create "Chat"       # cyan dot
./build/src/cli/stwctl task create "Design"     # yellow dot
```

### Quick Note Capture

```bash
# Press Super+N to open the quick note overlay
# Type a thought, press Enter to save, Escape to cancel

# View notes via IPC
./build/src/cli/stwctl -j '{"command": "task/notes"}'

# Set a sticky note (shows when you switch back to the task)
./build/src/cli/stwctl -j '{"command": "task/set-sticky", "text": "Fix the login bug"}'
```

### Breadcrumb Trail

Switch between several tasks, then query the history:

```bash
./build/src/cli/stwctl -j '{"command": "get/breadcrumbs"}'
# Returns last 16 task switches with timestamps
```

---

## Debugging

### Enable verbose logging

```bash
# Maximum debug output
WLR_LOG=debug ./build/src/compositor/singlethread 2>singlethread.log

# Watch logs in real-time
tail -f singlethread.log
```

### GDB

```bash
# Build with debug symbols
meson setup build --buildtype=debug --reconfigure
meson compile -C build

# Run under GDB
gdb ./build/src/compositor/singlethread
(gdb) run
```

### Valgrind (memory leaks)

```bash
valgrind --leak-check=full --show-leak-kinds=definite \
  ./build/src/compositor/singlethread 2>valgrind.log
```

### Common issues

| Problem | Solution |
|---------|----------|
| "Failed to open DRM device" | Run from TTY, not inside another compositor (or use `WLR_BACKENDS=wayland`) |
| "wlr_backend_autocreate failed" | Missing GPU drivers or seat access. Check `groups` includes `video` and `seat` |
| Panel doesn't appear | Check `SINGLETHREAD_SOCKET` env var is set. Panel needs IPC to connect |
| Shell components crash | Ensure `gtk4-layer-shell` is installed and compositor supports layer-shell |
| XWayland apps don't work | Build with `-Dxwayland=enabled` and ensure `xwayland` package is installed |
| Focus mode not responding | Check IPC: `stwctl -j '{"command": "get/focus-mode"}'` for errors |

### Inspecting the scene graph

```bash
# Dump the current scene tree (wlroots debug)
kill -USR1 $(pidof singlethread)
# Check stderr/logs for scene dump
```
