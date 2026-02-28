# SingleThread Troubleshooting

## Startup Issues

### Compositor won't start

**Check:** Are you running from a TTY or another Wayland/X11 session?

```bash
# From TTY (best):
singlethread

# With debug logging:
singlethread -d 2>&1 | tee /tmp/stw-debug.log
```

**Check:** Are all dependencies installed?

```bash
pacman -Qi wlroots libinput xkbcommon pixman json-c
```

**Check:** Do you have proper permissions?

```bash
# You should be in the 'input' and 'video' groups
groups $USER
# If missing:
sudo usermod -aG input,video $USER
# Then log out and back in
```

### Black screen after login

This usually means the compositor started but no shell components launched.

**Solution:** Add a startup command in your config or launch a terminal:

```bash
# From another TTY (Ctrl+Alt+F2), set WAYLAND_DISPLAY and run:
export WAYLAND_DISPLAY=wayland-1
foot &
```

### Config errors on startup

SingleThread will log config parse errors and fall back to defaults.

```bash
# Check your config syntax:
singlethread -d 2>&1 | grep -i "config\|error"
```

**Fix:** Invalid configs don't prevent startup. Fix the config and reload:
```bash
stwctl reload
```

## Window Issues

### Window assigned to wrong task

**Check rules:** Enable debug logging and look for rule matches:
```bash
singlethread -d 2>&1 | grep "Rule\|assigned"
```

**Check app_id:** Use `stwctl window list` to see the app_id of windows.
Some apps have unexpected app_id values.

### XWayland apps not working

**Check:** Is XWayland enabled?
```bash
# Should see XWayland in the logs:
singlethread -d 2>&1 | grep -i xwayland
```

**Check:** Is DISPLAY set?
```bash
echo $DISPLAY
# Should be something like :0 or :1
```

**Common fix:** Some XWayland apps need specific environment variables:
```bash
export GDK_BACKEND=x11  # Force GTK apps to use X11
export QT_QPA_PLATFORM=xcb  # Force Qt apps to use X11
```

### Window disappears after task switch

This is expected behavior. Windows belong to tasks and are hidden when
their task is not active. To keep a window visible across tasks:

```bash
# Use keybinding:
Super+g  # Toggle global for focused window

# Or via CLI:
stwctl window toggle-global
```

### Window won't tile

Some windows request floating by default (dialogs, tooltips, etc.).

**Override with a rule:**
```toml
[[rule]]
app_id = "problematic-app"
floating = false
```

## IPC Issues

### stwctl: "SINGLETHREAD_SOCKET not set"

The `SINGLETHREAD_SOCKET` environment variable must be set. It's automatically
set when the compositor starts for child processes.

**Fix:** If running stwctl from a separate terminal:
```bash
export SINGLETHREAD_SOCKET=$(ls /run/user/$UID/singlethread-ipc.*.sock 2>/dev/null | head -1)
```

### stwctl: "Cannot connect"

The compositor may not be running, or the socket file is stale.

**Fix:**
```bash
# Check if compositor is running:
pgrep singlethread

# Remove stale socket:
rm -f /run/user/$UID/singlethread-ipc.*.sock
```

## Performance Issues

### Stuttering / frame drops

**Check:** Hardware acceleration:
```bash
# The compositor should use GPU rendering:
singlethread -d 2>&1 | grep -i "renderer\|backend"
```

**Fix:** Reduce gaps and animations:
```toml
[general]
gaps_inner = 0
gaps_outer = 0
```

**Fix:** Check if a misbehaving client is causing issues:
```bash
stwctl window list  # Look for unusual windows
```

### Slow task switching with many windows

Task switching should be fast even with ~50 windows. If it's slow:

**Check:** Are there many unmapped (hidden) windows consuming resources?
```bash
stwctl window list | wc -l
```

## Logging

### View logs with journald

```bash
# If running as a systemd user service:
journalctl --user -u singlethread -f

# Or redirect to a file:
singlethread -d 2>&1 | tee ~/.local/state/singlethread/debug.log
```

### Log levels

- Normal: startup info, task creation/switching, errors
- Debug (`-d`): window assignment traces, rule matching, IPC messages,
  layout calculations

## Getting Help

1. Check the logs with `-d` flag
2. Check your config file for syntax errors
3. Try with the default config: `singlethread -c /dev/null`
4. Report issues with logs and config attached
