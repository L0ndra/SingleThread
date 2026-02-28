/*
 * SingleThread - Task-Centric Wayland Compositor
 * ipc.h - IPC socket protocol for scripting and CLI
 */
#ifndef STW_IPC_H
#define STW_IPC_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>

struct stw_server;

/* ─── IPC message types ────────────────────────────────────────── */
enum stw_ipc_command {
	/* Task commands */
	STW_IPC_TASK_LIST,
	STW_IPC_TASK_CREATE,
	STW_IPC_TASK_SWITCH,
	STW_IPC_TASK_RENAME,
	STW_IPC_TASK_CLOSE,
	STW_IPC_TASK_ARCHIVE,

	/* Window commands */
	STW_IPC_WINDOW_LIST,
	STW_IPC_WINDOW_FOCUS,
	STW_IPC_WINDOW_CLOSE,
	STW_IPC_WINDOW_TO_TASK,
	STW_IPC_WINDOW_TOGGLE_GLOBAL,
	STW_IPC_WINDOW_TOGGLE_FLOATING,
	STW_IPC_WINDOW_FULLSCREEN,

	/* Query commands */
	STW_IPC_GET_ACTIVE_TASK,
	STW_IPC_GET_ACTIVE_WINDOW,
	STW_IPC_GET_OUTPUTS,
	STW_IPC_GET_CONFIG,

	/* Layout commands */
	STW_IPC_LAYOUT_CYCLE,
	STW_IPC_LAYOUT_SET,

	/* General commands */
	STW_IPC_RELOAD_CONFIG,
	STW_IPC_VERSION,
	STW_IPC_EXIT,

	/* Launcher */
	STW_IPC_LAUNCH,
};

/* ─── IPC client connection ────────────────────────────────────── */
struct stw_ipc_client {
	struct wl_list link;           /* stw_server.ipc_clients */
	struct stw_server *server;
	int fd;
	struct wl_event_source *event_source;

	/* Read buffer */
	char *read_buf;
	size_t read_buf_size;
	size_t read_buf_len;
};

/* IPC lifecycle */
bool stw_ipc_init(struct stw_server *server);
void stw_ipc_finish(struct stw_server *server);

/* Get socket path */
char *stw_ipc_socket_path(void);

/* Send response to a client (JSON string) */
void stw_ipc_send(struct stw_ipc_client *client, const char *json);

/* Process a command from a client */
void stw_ipc_handle_command(struct stw_ipc_client *client,
	const char *json_str, size_t len);

#endif /* STW_IPC_H */
