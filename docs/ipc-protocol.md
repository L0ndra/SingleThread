# SingleThread IPC Protocol

## Overview

SingleThread provides a JSON-based IPC protocol over Unix domain sockets.
The socket path is set in the `SINGLETHREAD_SOCKET` environment variable.

## Wire Format

Messages use a simple length-prefixed format:

```
┌──────────────┬──────────────────────────┐
│ Length (4 B)  │ JSON payload (N bytes)   │
│ uint32, LE   │                          │
└──────────────┴──────────────────────────┘
```

1. Read 4 bytes as a little-endian uint32 → message length N
2. Read N bytes → JSON string

Both requests and responses use this format.

## Commands

### Task Commands

#### `task/list`
List all tasks.

**Request:**
```json
{ "command": "task/list" }
```

**Response:**
```json
{
  "success": true,
  "tasks": [
    {
      "id": 1,
      "name": "General",
      "active": true,
      "archived": false,
      "order": 0,
      "window_count": 3,
      "active_workspace": 0
    }
  ]
}
```

#### `task/create`
Create a new task.

**Request:**
```json
{ "command": "task/create", "name": "Development" }
```

#### `task/switch`
Switch to a task by ID or name.

**Request:**
```json
{ "command": "task/switch", "id": 1 }
// or
{ "command": "task/switch", "name": "Development" }
```

#### `task/rename`
Rename a task.

**Request:**
```json
{ "command": "task/rename", "id": 1, "name": "New Name" }
```

#### `task/close`
Close (destroy) a task. Windows are moved to another task.

**Request:**
```json
{ "command": "task/close", "id": 1 }
```

### Window Commands

#### `window/list`
List all mapped windows.

**Request:**
```json
{ "command": "window/list" }
```

**Response:**
```json
{
  "success": true,
  "windows": [
    {
      "app_id": "foot",
      "title": "fish",
      "global": false,
      "floating": false,
      "fullscreen": false,
      "mapped": true,
      "workspace": 0,
      "task_id": 1,
      "task_name": "General"
    }
  ]
}
```

#### `window/close`
Close the currently focused window.

**Request:**
```json
{ "command": "window/close" }
```

#### `window/to-task`
Move the focused window to a specific task.

**Request:**
```json
{ "command": "window/to-task", "task_id": 2 }
```

#### `window/toggle-global`
Toggle the global flag on the focused window.

**Request:**
```json
{ "command": "window/toggle-global" }
```

### Query Commands

#### `get/active-task`

**Request:**
```json
{ "command": "get/active-task" }
```

#### `get/active-window`

**Request:**
```json
{ "command": "get/active-window" }
```

#### `get/version`

**Request:**
```json
{ "command": "get/version" }
```

**Response:**
```json
{ "success": true, "version": "0.1.0" }
```

### General Commands

#### `reload-config`
Reload the configuration file.

```json
{ "command": "reload-config" }
```

#### `launch`
Launch an application (inherits current task).

```json
{ "command": "launch", "exec": "foot" }
```

#### `exit`
Shut down the compositor.

```json
{ "command": "exit" }
```

## Error Responses

All error responses include:
```json
{
  "success": false,
  "error": "Description of what went wrong"
}
```

## Example: Python Client

```python
import json
import socket
import struct
import os

def ipc_request(command):
    sock_path = os.environ['SINGLETHREAD_SOCKET']
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(sock_path)

    payload = json.dumps(command).encode()
    sock.send(struct.pack('<I', len(payload)))
    sock.send(payload)

    resp_len = struct.unpack('<I', sock.recv(4))[0]
    resp = sock.recv(resp_len).decode()
    sock.close()
    return json.loads(resp)

# List tasks
tasks = ipc_request({"command": "task/list"})
print(tasks)

# Create a task
ipc_request({"command": "task/create", "name": "Scripts"})
```
