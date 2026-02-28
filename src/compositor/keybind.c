/*
 * SingleThread - Task-Centric Wayland Compositor
 * keybind.c - Keybinding dispatch and action execution
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>

#include "keybind.h"
#include "server.h"
#include "view.h"
#include "task.h"
#include "layout.h"
#include "stw_config.h"

/* ─── Spawn helper ─────────────────────────────────────────────── */

void stw_spawn(const char *command) {
	if (!command || !*command) return;

	pid_t pid = fork();
	if (pid < 0) {
		wlr_log(WLR_ERROR, "fork failed for spawn");
		return;
	}
	if (pid == 0) {
		/* Child process */
		setsid();
		execl("/bin/sh", "/bin/sh", "-c", command, (char *)NULL);
		_exit(127);
	}
}

/* ─── Keybind handling ─────────────────────────────────────────── */

bool stw_keybind_handle(struct stw_server *server,
		uint32_t modifiers, xkb_keysym_t keysym) {
	struct stw_keybind *kb = stw_config_find_keybind(
		server->config, modifiers, keysym);
	if (!kb) return false;

	wlr_log(WLR_DEBUG, "Keybind matched: %s", kb->action);
	return stw_keybind_execute(server, kb->action);
}

/* Get the currently focused view */
static struct stw_view *get_focused_view(struct stw_server *server) {
	if (server->active_task) {
		return server->active_task->last_focused;
	}
	return NULL;
}

bool stw_keybind_execute(struct stw_server *server, const char *action) {
	if (!action) return false;

	/* ─── Task actions ─────────────────────────────────────────── */
	if (strcmp(action, "task:create") == 0) {
		char name[64];
		snprintf(name, sizeof(name), "Task %u", server->next_task_id);
		struct stw_task *task = stw_task_create(server, name);
		if (task) {
			stw_task_switch_to(server, task);
		}
		return true;
	}

	if (strcmp(action, "task:close") == 0) {
		if (server->active_task) {
			/* Don't delete the last task */
			int count = 0;
			struct stw_task *t;
			wl_list_for_each(t, &server->tasks, link) {
				if (!t->archived) count++;
			}
			if (count > 1) {
				stw_task_destroy(server->active_task);
			}
		}
		return true;
	}

	if (strcmp(action, "task:next") == 0) {
		stw_task_switch_next(server);
		return true;
	}

	if (strcmp(action, "task:prev") == 0) {
		stw_task_switch_prev(server);
		return true;
	}

	/* task:switch:N (1-indexed) */
	if (strncmp(action, "task:switch:", 12) == 0) {
		int idx = atoi(action + 12) - 1;
		if (idx >= 0) {
			stw_task_switch_by_index(server, idx);
		}
		return true;
	}

	/* ─── Window actions ───────────────────────────────────────── */
	if (strcmp(action, "window:close") == 0) {
		struct stw_view *view = get_focused_view(server);
		if (view) stw_view_close(view);
		return true;
	}

	if (strcmp(action, "window:fullscreen") == 0) {
		struct stw_view *view = get_focused_view(server);
		if (view) stw_view_toggle_fullscreen(view);
		return true;
	}

	if (strcmp(action, "window:toggle-floating") == 0) {
		struct stw_view *view = get_focused_view(server);
		if (view) stw_view_toggle_floating(view);
		return true;
	}

	if (strcmp(action, "window:toggle-global") == 0) {
		struct stw_view *view = get_focused_view(server);
		if (view) stw_view_toggle_global(view);
		return true;
	}

	/* window:to-task:N (1-indexed) */
	if (strncmp(action, "window:to-task:", 15) == 0) {
		int idx = atoi(action + 15) - 1;
		struct stw_view *view = get_focused_view(server);
		if (view && idx >= 0) {
			int i = 0;
			struct stw_task *task;
			wl_list_for_each(task, &server->tasks, link) {
				if (!task->archived) {
					if (i == idx) {
						stw_view_assign_task(view, task,
							STW_ASSIGN_MANUAL);
						break;
					}
					i++;
				}
			}
		}
		return true;
	}

	/* window:to-workspace:N (1-indexed) */
	if (strncmp(action, "window:to-workspace:", 20) == 0) {
		int idx = atoi(action + 20) - 1;
		struct stw_view *view = get_focused_view(server);
		if (view && idx >= 0) {
			view->workspace_idx = idx;
			stw_layout_arrange_all(server);
		}
		return true;
	}

	/* Focus direction */
	if (strcmp(action, "window:focus:left") == 0) {
		stw_layout_focus_direction(server, STW_DIR_LEFT);
		return true;
	}
	if (strcmp(action, "window:focus:right") == 0) {
		stw_layout_focus_direction(server, STW_DIR_RIGHT);
		return true;
	}
	if (strcmp(action, "window:focus:up") == 0) {
		stw_layout_focus_direction(server, STW_DIR_UP);
		return true;
	}
	if (strcmp(action, "window:focus:down") == 0) {
		stw_layout_focus_direction(server, STW_DIR_DOWN);
		return true;
	}

	/* Swap direction */
	if (strcmp(action, "window:swap:left") == 0) {
		stw_layout_swap_direction(server, STW_DIR_LEFT);
		return true;
	}
	if (strcmp(action, "window:swap:right") == 0) {
		stw_layout_swap_direction(server, STW_DIR_RIGHT);
		return true;
	}
	if (strcmp(action, "window:swap:up") == 0) {
		stw_layout_swap_direction(server, STW_DIR_UP);
		return true;
	}
	if (strcmp(action, "window:swap:down") == 0) {
		stw_layout_swap_direction(server, STW_DIR_DOWN);
		return true;
	}

	if (strcmp(action, "window:zoom") == 0) {
		stw_layout_zoom(server);
		return true;
	}

	/* ─── Layout actions ───────────────────────────────────────── */
	if (strcmp(action, "layout:master-ratio:increase") == 0) {
		stw_layout_increase_master_ratio(server, 0.05);
		return true;
	}
	if (strcmp(action, "layout:master-ratio:decrease") == 0) {
		stw_layout_decrease_master_ratio(server, 0.05);
		return true;
	}
	if (strcmp(action, "layout:master-count:increase") == 0) {
		stw_layout_increase_master_count(server);
		return true;
	}
	if (strcmp(action, "layout:master-count:decrease") == 0) {
		stw_layout_decrease_master_count(server);
		return true;
	}
	if (strcmp(action, "layout:cycle") == 0) {
		stw_layout_cycle(server);
		return true;
	}

	/* ─── Workspace actions ────────────────────────────────────── */
	if (strncmp(action, "workspace:switch:", 17) == 0) {
		int idx = atoi(action + 17) - 1;
		if (server->active_task && idx >= 0) {
			stw_task_switch_workspace(server->active_task, idx);
		}
		return true;
	}

	/* ─── Shell actions ────────────────────────────────────────── */
	if (strcmp(action, "launcher:open") == 0 ||
			strcmp(action, "switcher:open") == 0 ||
			strcmp(action, "overview:open") == 0) {
		/* These will be handled by sending IPC to shell components */
		/* For now, log it */
		wlr_log(WLR_DEBUG, "Shell action: %s (TODO)", action);
		return true;
	}

	/* ─── Compositor actions ───────────────────────────────────── */
	if (strcmp(action, "compositor:exit") == 0) {
		wl_display_terminate(server->wl_display);
		return true;
	}

	if (strcmp(action, "compositor:reload") == 0) {
		stw_config_reload(server);
		return true;
	}

	/* ─── Spawn actions ────────────────────────────────────────── */
	if (strncmp(action, "spawn:", 6) == 0) {
		stw_spawn(action + 6);
		return true;
	}

	wlr_log(WLR_ERROR, "Unknown action: %s", action);
	return false;
}
