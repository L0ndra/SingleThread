/*
 * SingleThread - Task-Centric Wayland Compositor
 * session.c - Session persistence and restore
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <json-c/json.h>
#include <wlr/util/log.h>

#include "session.h"
#include "server.h"
#include "task.h"
#include "view.h"

char *stw_session_path(void) {
	char path[PATH_MAX];
	const char *xdg_state = getenv("XDG_STATE_HOME");
	if (xdg_state) {
		snprintf(path, sizeof(path), "%s/singlethread/session.json",
			xdg_state);
	} else {
		const char *home = getenv("HOME");
		if (!home) return NULL;
		snprintf(path, sizeof(path),
			"%s/.local/state/singlethread/session.json", home);
	}
	return strdup(path);
}

static bool ensure_dir(const char *path) {
	char dir[PATH_MAX];
	snprintf(dir, sizeof(dir), "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash) {
		*slash = '\0';
		if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
			wlr_log(WLR_ERROR, "Failed to create directory: %s: %s",
				dir, strerror(errno));
			return false;
		}
	}
	return true;
}

bool stw_session_save(struct stw_server *server) {
	if (!server->session_path) return false;

	struct json_object *root = json_object_new_object();
	json_object_object_add(root, "version",
		json_object_new_int(1));

	/* Save tasks */
	struct json_object *tasks_arr = json_object_new_array();
	struct stw_task *task;
	wl_list_for_each(task, &server->tasks, link) {
		struct json_object *task_obj = json_object_new_object();
		json_object_object_add(task_obj, "id",
			json_object_new_int(task->id));
		json_object_object_add(task_obj, "name",
			json_object_new_string(task->name));
		json_object_object_add(task_obj, "order",
			json_object_new_int(task->order));
		json_object_object_add(task_obj, "archived",
			json_object_new_boolean(task->archived));
		json_object_object_add(task_obj, "active_workspace",
			json_object_new_int(task->active_workspace));

		if (task->icon) {
			json_object_object_add(task_obj, "icon",
				json_object_new_string(task->icon));
		}
		if (task->layout_name) {
			json_object_object_add(task_obj, "layout",
				json_object_new_string(task->layout_name));
		}

		/* Save workspace state */
		struct json_object *ws_arr = json_object_new_array();
		for (int i = 0; i < task->workspace_count; i++) {
			struct json_object *ws_obj = json_object_new_object();
			json_object_object_add(ws_obj, "master_count",
				json_object_new_int(
					task->workspaces[i].master_count));
			json_object_object_add(ws_obj, "master_ratio",
				json_object_new_double(
					task->workspaces[i].master_ratio));
			json_object_array_add(ws_arr, ws_obj);
		}
		json_object_object_add(task_obj, "workspaces", ws_arr);

		/* Save pinned apps */
		struct json_object *apps_arr = json_object_new_array();
		struct stw_pinned_app *app;
		wl_list_for_each(app, &task->pinned_apps, link) {
			struct json_object *app_obj = json_object_new_object();
			if (app->desktop_id) {
				json_object_object_add(app_obj, "desktop_id",
					json_object_new_string(app->desktop_id));
			}
			if (app->exec) {
				json_object_object_add(app_obj, "exec",
					json_object_new_string(app->exec));
			}
			if (app->name) {
				json_object_object_add(app_obj, "name",
					json_object_new_string(app->name));
			}
			json_object_array_add(apps_arr, app_obj);
		}
		json_object_object_add(task_obj, "pinned_apps", apps_arr);

		json_object_array_add(tasks_arr, task_obj);
	}
	json_object_object_add(root, "tasks", tasks_arr);

	/* Save active task ID */
	if (server->active_task) {
		json_object_object_add(root, "active_task_id",
			json_object_new_int(server->active_task->id));
	}

	/* Write to file */
	if (!ensure_dir(server->session_path)) {
		json_object_put(root);
		return false;
	}

	const char *json_str = json_object_to_json_string_ext(root,
		JSON_C_TO_STRING_PRETTY);

	FILE *f = fopen(server->session_path, "w");
	if (!f) {
		wlr_log(WLR_ERROR, "Failed to save session: %s: %s",
			server->session_path, strerror(errno));
		json_object_put(root);
		return false;
	}

	fprintf(f, "%s\n", json_str);
	fclose(f);
	json_object_put(root);

	wlr_log(WLR_INFO, "Session saved: %s", server->session_path);
	return true;
}

bool stw_session_restore(struct stw_server *server) {
	if (!server->session_path) return false;

	FILE *f = fopen(server->session_path, "r");
	if (!f) {
		wlr_log(WLR_DEBUG, "No session file to restore: %s",
			server->session_path);
		return false;
	}

	/* Read entire file */
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (fsize <= 0 || fsize > 10 * 1024 * 1024) {
		fclose(f);
		return false;
	}

	char *buf = malloc(fsize + 1);
	if (!buf) {
		fclose(f);
		return false;
	}
	fread(buf, 1, fsize, f);
	buf[fsize] = '\0';
	fclose(f);

	struct json_object *root = json_tokener_parse(buf);
	free(buf);

	if (!root) {
		wlr_log(WLR_ERROR, "Failed to parse session file");
		return false;
	}

	/* Restore tasks */
	struct json_object *tasks_arr;
	if (json_object_object_get_ex(root, "tasks", &tasks_arr)) {
		int len = json_object_array_length(tasks_arr);
		for (int i = 0; i < len; i++) {
			struct json_object *task_obj =
				json_object_array_get_idx(tasks_arr, i);

			struct json_object *name_obj;
			if (!json_object_object_get_ex(task_obj, "name",
					&name_obj)) {
				continue;
			}

			struct stw_task *task = stw_task_create(server,
				json_object_get_string(name_obj));
			if (!task) continue;

			struct json_object *tmp;
			if (json_object_object_get_ex(task_obj, "order", &tmp)) {
				task->order = json_object_get_int(tmp);
			}
			if (json_object_object_get_ex(task_obj, "archived", &tmp)) {
				task->archived = json_object_get_boolean(tmp);
			}
			if (json_object_object_get_ex(task_obj,
					"active_workspace", &tmp)) {
				task->active_workspace = json_object_get_int(tmp);
			}
			if (json_object_object_get_ex(task_obj, "icon", &tmp)) {
				task->icon = strdup(json_object_get_string(tmp));
			}
			if (json_object_object_get_ex(task_obj, "layout", &tmp)) {
				task->layout_name =
					strdup(json_object_get_string(tmp));
			}

			/* Restore workspace state */
			struct json_object *ws_arr;
			if (json_object_object_get_ex(task_obj,
					"workspaces", &ws_arr)) {
				int ws_len = json_object_array_length(ws_arr);
				for (int w = 0; w < ws_len &&
						w < STW_MAX_WORKSPACES; w++) {
					struct json_object *ws_obj =
						json_object_array_get_idx(ws_arr, w);
					if (json_object_object_get_ex(ws_obj,
							"master_count", &tmp)) {
						task->workspaces[w].master_count =
							json_object_get_int(tmp);
					}
					if (json_object_object_get_ex(ws_obj,
							"master_ratio", &tmp)) {
						task->workspaces[w].master_ratio =
							json_object_get_double(tmp);
					}
				}
			}

			/* Restore pinned apps */
			struct json_object *apps_arr;
			if (json_object_object_get_ex(task_obj,
					"pinned_apps", &apps_arr)) {
				int apps_len = json_object_array_length(apps_arr);
				for (int a = 0; a < apps_len; a++) {
					struct json_object *app_obj =
						json_object_array_get_idx(apps_arr, a);
					const char *desktop_id = NULL, *exec = NULL;
					const char *app_name = NULL;
					if (json_object_object_get_ex(app_obj,
							"desktop_id", &tmp)) {
						desktop_id = json_object_get_string(tmp);
					}
					if (json_object_object_get_ex(app_obj,
							"exec", &tmp)) {
						exec = json_object_get_string(tmp);
					}
					if (json_object_object_get_ex(app_obj,
							"name", &tmp)) {
						app_name = json_object_get_string(tmp);
					}
					stw_task_add_pinned_app(task,
						desktop_id, exec, app_name);
				}
			}
		}
	}

	/* Restore active task */
	struct json_object *active_obj;
	if (json_object_object_get_ex(root, "active_task_id", &active_obj)) {
		uint32_t active_id = json_object_get_int(active_obj);
		struct stw_task *task = stw_task_find_by_id(server, active_id);
		if (task) {
			stw_task_switch_to(server, task);
		}
	} else if (!wl_list_empty(&server->tasks)) {
		/* Activate first task */
		struct stw_task *first =
			wl_container_of(server->tasks.next, first, link);
		stw_task_switch_to(server, first);
	}

	json_object_put(root);
	wlr_log(WLR_INFO, "Session restored: %s", server->session_path);
	return true;
}

/* ─── Auto-save ────────────────────────────────────────────────── */

static struct wl_event_source *autosave_timer = NULL;

static int autosave_handler(void *data) {
	struct stw_server *server = data;
	stw_session_save(server);
	/* Re-arm the timer */
	if (autosave_timer) {
		wl_event_source_timer_update(autosave_timer, 60 * 1000);
	}
	return 0;
}

bool stw_session_start_autosave(struct stw_server *server,
		int interval_sec) {
	autosave_timer = wl_event_loop_add_timer(
		server->wl_event_loop, autosave_handler, server);
	if (!autosave_timer) return false;

	wl_event_source_timer_update(autosave_timer, interval_sec * 1000);
	wlr_log(WLR_INFO, "Session autosave enabled (every %ds)", interval_sec);
	return true;
}

void stw_session_stop_autosave(struct stw_server *server) {
	(void)server;
	if (autosave_timer) {
		wl_event_source_remove(autosave_timer);
		autosave_timer = NULL;
	}
}
