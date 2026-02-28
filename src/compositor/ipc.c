/*
 * SingleThread - Task-Centric Wayland Compositor
 * ipc.c - IPC socket protocol (JSON over Unix socket)
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <json-c/json.h>
#include <wlr/util/log.h>

#include "ipc.h"
#include "server.h"
#include "view.h"
#include "task.h"
#include "keybind.h"
#include "stw_config.h"

/* ─── IPC socket path ──────────────────────────────────────────── */

char *stw_ipc_socket_path(void) {
	const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
	if (!xdg_runtime) {
		xdg_runtime = "/tmp";
	}

	char path[256];
	snprintf(path, sizeof(path), "%s/singlethread-ipc.%d.sock",
		xdg_runtime, getpid());
	return strdup(path);
}

/* ─── Client management ───────────────────────────────────────── */

static void ipc_client_destroy(struct stw_ipc_client *client) {
	wl_list_remove(&client->link);
	if (client->event_source) {
		wl_event_source_remove(client->event_source);
	}
	close(client->fd);
	free(client->read_buf);
	free(client);
}

void stw_ipc_send(struct stw_ipc_client *client, const char *json) {
	size_t len = strlen(json);
	/* Protocol: 4-byte length prefix + JSON payload */
	uint32_t net_len = (uint32_t)len;
	if (write(client->fd, &net_len, 4) < 0) return;
	if (write(client->fd, json, len) < 0) return;
}

/* ─── JSON response helpers ────────────────────────────────────── */

static void send_ok(struct stw_ipc_client *client, const char *message) {
	struct json_object *resp = json_object_new_object();
	json_object_object_add(resp, "success",
		json_object_new_boolean(true));
	if (message) {
		json_object_object_add(resp, "message",
			json_object_new_string(message));
	}
	stw_ipc_send(client, json_object_to_json_string(resp));
	json_object_put(resp);
}

static void send_error(struct stw_ipc_client *client, const char *error) {
	struct json_object *resp = json_object_new_object();
	json_object_object_add(resp, "success",
		json_object_new_boolean(false));
	json_object_object_add(resp, "error",
		json_object_new_string(error));
	stw_ipc_send(client, json_object_to_json_string(resp));
	json_object_put(resp);
}

/* ─── Task JSON serialization ──────────────────────────────────── */

static struct json_object *task_to_json(struct stw_task *task) {
	struct json_object *obj = json_object_new_object();
	json_object_object_add(obj, "id",
		json_object_new_int(task->id));
	json_object_object_add(obj, "name",
		json_object_new_string(task->name));
	json_object_object_add(obj, "active",
		json_object_new_boolean(task->active));
	json_object_object_add(obj, "archived",
		json_object_new_boolean(task->archived));
	json_object_object_add(obj, "order",
		json_object_new_int(task->order));
	json_object_object_add(obj, "window_count",
		json_object_new_int(stw_task_count_views(task)));
	json_object_object_add(obj, "active_workspace",
		json_object_new_int(task->active_workspace));
	return obj;
}

/* ─── View JSON serialization ──────────────────────────────────── */

static struct json_object *view_to_json(struct stw_view *view) {
	struct json_object *obj = json_object_new_object();
	json_object_object_add(obj, "app_id",
		json_object_new_string(view->app_id ? view->app_id : ""));
	json_object_object_add(obj, "title",
		json_object_new_string(view->title ? view->title : ""));
	json_object_object_add(obj, "global",
		json_object_new_boolean(view->is_global));
	json_object_object_add(obj, "floating",
		json_object_new_boolean(view->state == STW_VIEW_FLOATING));
	json_object_object_add(obj, "fullscreen",
		json_object_new_boolean(view->state == STW_VIEW_FULLSCREEN));
	json_object_object_add(obj, "mapped",
		json_object_new_boolean(view->mapped));
	json_object_object_add(obj, "workspace",
		json_object_new_int(view->workspace_idx));
	if (view->task) {
		json_object_object_add(obj, "task_id",
			json_object_new_int(view->task->id));
		json_object_object_add(obj, "task_name",
			json_object_new_string(view->task->name));
	}
	return obj;
}

/* ─── Command handlers ─────────────────────────────────────────── */

void stw_ipc_handle_command(struct stw_ipc_client *client,
		const char *json_str, size_t len) {
	struct stw_server *server = client->server;

	struct json_object *req = json_tokener_parse(json_str);
	if (!req) {
		send_error(client, "Invalid JSON");
		return;
	}

	struct json_object *cmd_obj;
	if (!json_object_object_get_ex(req, "command", &cmd_obj)) {
		send_error(client, "Missing 'command' field");
		json_object_put(req);
		return;
	}

	const char *cmd = json_object_get_string(cmd_obj);
	wlr_log(WLR_DEBUG, "IPC command: %s", cmd);

	/* ─── Task commands ────────────────────────────────────────── */

	if (strcmp(cmd, "task/list") == 0) {
		struct json_object *resp = json_object_new_object();
		json_object_object_add(resp, "success",
			json_object_new_boolean(true));
		struct json_object *arr = json_object_new_array();
		struct stw_task *task;
		wl_list_for_each(task, &server->tasks, link) {
			json_object_array_add(arr, task_to_json(task));
		}
		json_object_object_add(resp, "tasks", arr);
		stw_ipc_send(client, json_object_to_json_string(resp));
		json_object_put(resp);
	}

	else if (strcmp(cmd, "task/create") == 0) {
		struct json_object *name_obj;
		const char *name = "New Task";
		if (json_object_object_get_ex(req, "name", &name_obj)) {
			name = json_object_get_string(name_obj);
		}
		struct stw_task *task = stw_task_create(server, name);
		if (task) {
			struct json_object *resp = json_object_new_object();
			json_object_object_add(resp, "success",
				json_object_new_boolean(true));
			json_object_object_add(resp, "task", task_to_json(task));
			stw_ipc_send(client, json_object_to_json_string(resp));
			json_object_put(resp);
		} else {
			send_error(client, "Failed to create task");
		}
	}

	else if (strcmp(cmd, "task/switch") == 0) {
		struct json_object *id_obj;
		if (json_object_object_get_ex(req, "id", &id_obj)) {
			uint32_t id = json_object_get_int(id_obj);
			struct stw_task *task = stw_task_find_by_id(server, id);
			if (task) {
				stw_task_switch_to(server, task);
				send_ok(client, "Switched task");
			} else {
				send_error(client, "Task not found");
			}
		} else {
			struct json_object *name_obj;
			if (json_object_object_get_ex(req, "name", &name_obj)) {
				const char *name = json_object_get_string(name_obj);
				struct stw_task *task =
					stw_task_find_by_name(server, name);
				if (task) {
					stw_task_switch_to(server, task);
					send_ok(client, "Switched task");
				} else {
					send_error(client, "Task not found");
				}
			} else {
				send_error(client, "Missing 'id' or 'name'");
			}
		}
	}

	else if (strcmp(cmd, "task/rename") == 0) {
		struct json_object *id_obj, *name_obj;
		if (json_object_object_get_ex(req, "id", &id_obj) &&
				json_object_object_get_ex(req, "name", &name_obj)) {
			struct stw_task *task = stw_task_find_by_id(server,
				json_object_get_int(id_obj));
			if (task) {
				stw_task_rename(task,
					json_object_get_string(name_obj));
				send_ok(client, "Task renamed");
			} else {
				send_error(client, "Task not found");
			}
		} else {
			send_error(client, "Missing 'id' and 'name'");
		}
	}

	else if (strcmp(cmd, "task/close") == 0) {
		struct json_object *id_obj;
		if (json_object_object_get_ex(req, "id", &id_obj)) {
			struct stw_task *task = stw_task_find_by_id(server,
				json_object_get_int(id_obj));
			if (task) {
				stw_task_destroy(task);
				send_ok(client, "Task closed");
			} else {
				send_error(client, "Task not found");
			}
		} else {
			send_error(client, "Missing 'id'");
		}
	}

	/* ─── Window commands ──────────────────────────────────────── */

	else if (strcmp(cmd, "window/list") == 0) {
		struct json_object *resp = json_object_new_object();
		json_object_object_add(resp, "success",
			json_object_new_boolean(true));
		struct json_object *arr = json_object_new_array();
		struct stw_view *view;
		wl_list_for_each(view, &server->views, link) {
			if (view->mapped) {
				json_object_array_add(arr, view_to_json(view));
			}
		}
		json_object_object_add(resp, "windows", arr);
		stw_ipc_send(client, json_object_to_json_string(resp));
		json_object_put(resp);
	}

	else if (strcmp(cmd, "window/close") == 0) {
		struct stw_view *view = NULL;
		if (server->active_task) {
			view = server->active_task->last_focused;
		}
		if (view) {
			stw_view_close(view);
			send_ok(client, "Window closed");
		} else {
			send_error(client, "No focused window");
		}
	}

	else if (strcmp(cmd, "window/to-task") == 0) {
		struct json_object *task_id_obj;
		if (json_object_object_get_ex(req, "task_id", &task_id_obj)) {
			struct stw_task *task = stw_task_find_by_id(server,
				json_object_get_int(task_id_obj));
			struct stw_view *view = NULL;
			if (server->active_task) {
				view = server->active_task->last_focused;
			}
			if (task && view) {
				stw_view_assign_task(view, task, STW_ASSIGN_MANUAL);
				send_ok(client, "Window moved to task");
			} else {
				send_error(client, "Task or window not found");
			}
		}
	}

	else if (strcmp(cmd, "window/toggle-global") == 0) {
		struct stw_view *view = NULL;
		if (server->active_task) {
			view = server->active_task->last_focused;
		}
		if (view) {
			stw_view_toggle_global(view);
			send_ok(client, view->is_global ?
				"Window is now global" :
				"Window is no longer global");
		} else {
			send_error(client, "No focused window");
		}
	}

	/* ─── Query commands ───────────────────────────────────────── */

	else if (strcmp(cmd, "get/active-task") == 0) {
		struct json_object *resp = json_object_new_object();
		json_object_object_add(resp, "success",
			json_object_new_boolean(true));
		if (server->active_task) {
			json_object_object_add(resp, "task",
				task_to_json(server->active_task));
		}
		stw_ipc_send(client, json_object_to_json_string(resp));
		json_object_put(resp);
	}

	else if (strcmp(cmd, "get/active-window") == 0) {
		struct json_object *resp = json_object_new_object();
		json_object_object_add(resp, "success",
			json_object_new_boolean(true));
		struct stw_view *view = NULL;
		if (server->active_task) {
			view = server->active_task->last_focused;
		}
		if (view) {
			json_object_object_add(resp, "window",
				view_to_json(view));
		}
		stw_ipc_send(client, json_object_to_json_string(resp));
		json_object_put(resp);
	}

	else if (strcmp(cmd, "get/version") == 0) {
		struct json_object *resp = json_object_new_object();
		json_object_object_add(resp, "success",
			json_object_new_boolean(true));
		json_object_object_add(resp, "version",
			json_object_new_string(STW_VERSION));
		stw_ipc_send(client, json_object_to_json_string(resp));
		json_object_put(resp);
	}

	/* ─── General commands ─────────────────────────────────────── */

	else if (strcmp(cmd, "reload-config") == 0) {
		stw_config_reload(server);
		send_ok(client, "Config reloaded");
	}

	else if (strcmp(cmd, "exit") == 0) {
		send_ok(client, "Shutting down");
		wl_display_terminate(server->wl_display);
	}

	else if (strcmp(cmd, "launch") == 0) {
		struct json_object *exec_obj;
		if (json_object_object_get_ex(req, "exec", &exec_obj)) {
			stw_spawn(json_object_get_string(exec_obj));
			send_ok(client, "Launched");
		} else {
			send_error(client, "Missing 'exec'");
		}
	}

	else {
		send_error(client, "Unknown command");
	}

	json_object_put(req);
}

/* ─── Socket event handling ────────────────────────────────────── */

static int ipc_client_handler(int fd, uint32_t mask, void *data) {
	struct stw_ipc_client *client = data;

	if (mask & WL_EVENT_HANGUP || mask & WL_EVENT_ERROR) {
		ipc_client_destroy(client);
		return 0;
	}

	if (mask & WL_EVENT_READABLE) {
		/* Read length prefix */
		uint32_t msg_len;
		ssize_t n = recv(fd, &msg_len, 4, MSG_PEEK);
		if (n <= 0) {
			ipc_client_destroy(client);
			return 0;
		}
		if (n < 4) return 0; /* Wait for full header */

		/* Read the full message */
		recv(fd, &msg_len, 4, 0);

		if (msg_len > 1024 * 1024) {
			wlr_log(WLR_ERROR, "IPC message too large: %u", msg_len);
			ipc_client_destroy(client);
			return 0;
		}

		char *buf = malloc(msg_len + 1);
		if (!buf) {
			ipc_client_destroy(client);
			return 0;
		}

		size_t total = 0;
		while (total < msg_len) {
			n = recv(fd, buf + total, msg_len - total, 0);
			if (n <= 0) {
				free(buf);
				ipc_client_destroy(client);
				return 0;
			}
			total += n;
		}
		buf[msg_len] = '\0';

		stw_ipc_handle_command(client, buf, msg_len);
		free(buf);
	}

	return 0;
}

static int ipc_accept_handler(int fd, uint32_t mask, void *data) {
	struct stw_server *server = data;
	(void)mask;

	int client_fd = accept(fd, NULL, NULL);
	if (client_fd < 0) {
		wlr_log(WLR_ERROR, "IPC accept failed: %s", strerror(errno));
		return 0;
	}

	struct stw_ipc_client *client = calloc(1, sizeof(*client));
	if (!client) {
		close(client_fd);
		return 0;
	}

	client->server = server;
	client->fd = client_fd;
	client->event_source = wl_event_loop_add_fd(
		server->wl_event_loop, client_fd,
		WL_EVENT_READABLE, ipc_client_handler, client);

	wl_list_insert(&server->ipc_clients, &client->link);

	wlr_log(WLR_DEBUG, "IPC client connected");
	return 0;
}

/* ─── IPC lifecycle ────────────────────────────────────────────── */

bool stw_ipc_init(struct stw_server *server) {
	char *socket_path = stw_ipc_socket_path();
	if (!socket_path) return false;

	/* Remove stale socket */
	unlink(socket_path);

	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		wlr_log(WLR_ERROR, "Failed to create IPC socket: %s",
			strerror(errno));
		free(socket_path);
		return false;
	}

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		wlr_log(WLR_ERROR, "Failed to bind IPC socket: %s",
			strerror(errno));
		close(fd);
		free(socket_path);
		return false;
	}

	if (listen(fd, 5) < 0) {
		wlr_log(WLR_ERROR, "Failed to listen on IPC socket: %s",
			strerror(errno));
		close(fd);
		unlink(socket_path);
		free(socket_path);
		return false;
	}

	server->ipc_socket_fd = fd;
	server->ipc_event_source = wl_event_loop_add_fd(
		server->wl_event_loop, fd,
		WL_EVENT_READABLE, ipc_accept_handler, server);

	/* Set environment variable for clients */
	setenv("SINGLETHREAD_SOCKET", socket_path, true);

	wlr_log(WLR_INFO, "IPC listening on: %s", socket_path);
	free(socket_path);
	return true;
}

void stw_ipc_finish(struct stw_server *server) {
	/* Destroy all clients */
	struct stw_ipc_client *client, *tmp;
	wl_list_for_each_safe(client, tmp, &server->ipc_clients, link) {
		ipc_client_destroy(client);
	}

	if (server->ipc_event_source) {
		wl_event_source_remove(server->ipc_event_source);
	}
	if (server->ipc_socket_fd >= 0) {
		close(server->ipc_socket_fd);
	}

	/* Clean up socket file */
	char *path = stw_ipc_socket_path();
	if (path) {
		unlink(path);
		free(path);
	}
}
