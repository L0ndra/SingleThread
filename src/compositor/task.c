/*
 * SingleThread - Task-Centric Wayland Compositor
 * task.c - Task (context) system implementation
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>

#include "task.h"
#include "server.h"
#include "view.h"
#include "layout.h"
#include "focus_mode.h"

/* ─── Task lifecycle ───────────────────────────────────────────── */

struct stw_task *stw_task_create(struct stw_server *server, const char *name) {
	assert(server);
	assert(name);

	/* Check task limit */
	if (server->config->task_max_tasks > 0) {
		int count = 0;
		struct stw_task *t;
		wl_list_for_each(t, &server->tasks, link) {
			if (!t->archived) count++;
		}
		if (count >= server->config->task_max_tasks) {
			wlr_log(WLR_ERROR, "Task limit reached (%d)",
				server->config->task_max_tasks);
			return NULL;
		}
	}

	struct stw_task *task = calloc(1, sizeof(*task));
	if (!task) {
		wlr_log(WLR_ERROR, "Failed to allocate task");
		return NULL;
	}

	task->server = server;
	task->id = server->next_task_id++;
	task->name = strdup(name);
	task->active = false;
	task->archived = false;
	wl_list_init(&task->views);
	wl_list_init(&task->pinned_apps);
	wl_list_init(&task->notes);

	/* Assign accent color from palette (cycle through 8 colors) */
	static const uint32_t palette[] = {
		0x7aa2f7ff, /* blue */
		0xbb9af7ff, /* purple */
		0x7dcfffff, /* cyan */
		0xe0af68ff, /* yellow */
		0x9ece6aff, /* green */
		0xf7768eff, /* red */
		0xff9e64ff, /* orange */
		0x73dacaff, /* teal */
	};
	task->accent_color = palette[(server->next_task_id - 1) % 8];

	/* Determine order (append at end) */
	int max_order = 0;
	struct stw_task *t;
	wl_list_for_each(t, &server->tasks, link) {
		if (t->order >= max_order) {
			max_order = t->order + 1;
		}
	}
	task->order = max_order;

	/* Initialize workspaces */
	task->workspace_count = 3; /* Default 3 workspaces per task */
	task->active_workspace = 0;
	for (int i = 0; i < task->workspace_count; i++) {
		task->workspaces[i].index = i;
		task->workspaces[i].master_count =
			server->config->master_count;
		task->workspaces[i].master_ratio =
			server->config->master_ratio;
	}

	wl_list_insert(server->tasks.prev, &task->link);

	wlr_log(WLR_INFO, "Task created: id=%u name='%s' order=%d",
		task->id, task->name, task->order);

	return task;
}

void stw_task_destroy(struct stw_task *task) {
	assert(task);

	wlr_log(WLR_INFO, "Task destroyed: id=%u name='%s'",
		task->id, task->name);

	/* Move all views to the next available task or unassign */
	struct stw_view *view, *view_tmp;
	wl_list_for_each_safe(view, view_tmp, &task->views, task_link) {
		wl_list_remove(&view->task_link);
		wl_list_init(&view->task_link);
		view->task = NULL;

		/* Try to reassign to another task */
		struct stw_task *other;
		wl_list_for_each(other, &task->server->tasks, link) {
			if (other != task && !other->archived) {
				stw_view_assign_task(view, other, STW_ASSIGN_DEFAULT);
				break;
			}
		}
	}

	/* Clean up pinned apps */
	struct stw_pinned_app *app, *app_tmp;
	wl_list_for_each_safe(app, app_tmp, &task->pinned_apps, link) {
		wl_list_remove(&app->link);
		free(app->desktop_id);
		free(app->exec);
		free(app->name);
		free(app);
	}

	/* If this was the active task, switch to another */
	if (task->server->active_task == task) {
		task->server->active_task = NULL;
		struct stw_task *next;
		wl_list_for_each(next, &task->server->tasks, link) {
			if (next != task && !next->archived) {
				stw_task_switch_to(task->server, next);
				break;
			}
		}
	}

	/* Clean up workspace names */
	for (int i = 0; i < task->workspace_count; i++) {
		free(task->workspaces[i].name);
	}

	/* Clean up quick notes */
	struct stw_quick_note *note, *note_tmp;
	wl_list_for_each_safe(note, note_tmp, &task->notes, link) {
		wl_list_remove(&note->link);
		free(note->text);
		free(note);
	}

	wl_list_remove(&task->link);
	free(task->name);
	free(task->icon);
	free(task->layout_name);
	free(task->sticky_note);
	free(task);
}

void stw_task_rename(struct stw_task *task, const char *name) {
	assert(task);
	assert(name);
	free(task->name);
	task->name = strdup(name);
	wlr_log(WLR_INFO, "Task renamed: id=%u name='%s'", task->id, task->name);
}

/* ─── Task switching ───────────────────────────────────────────── */

void stw_task_activate(struct stw_task *task) {
	assert(task);
	task->active = true;

	/* Start task timer */
	task->timer_switch_in = time(NULL);

	/* Show all views belonging to this task */
	struct stw_view *view;
	wl_list_for_each(view, &task->views, task_link) {
		if (view->mapped) {
			stw_view_set_visible(view, true);
		}
	}
}

void stw_task_deactivate(struct stw_task *task) {
	assert(task);
	task->active = false;

	/* Pause task timer */
	if (task->timer_switch_in > 0) {
		task->timer_total_secs += (uint64_t)difftime(time(NULL),
			task->timer_switch_in);
		task->timer_switch_in = 0;
	}

	/* Hide all non-global views belonging to this task */
	struct stw_view *view;
	wl_list_for_each(view, &task->views, task_link) {
		if (!view->is_global) {
			stw_view_set_visible(view, false);
		}
	}
}

void stw_task_switch_to(struct stw_server *server, struct stw_task *task) {
	assert(server);
	assert(task);

	if (server->active_task == task) {
		return; /* Already active */
	}

	wlr_log(WLR_INFO, "Switching to task '%s' (id=%u)", task->name, task->id);

	/* Record in breadcrumb trail */
	stw_breadcrumb_record(&server->breadcrumbs, task->id, task->name);

	/* Deactivate current task */
	if (server->active_task) {
		stw_task_deactivate(server->active_task);
	}

	/* Activate new task */
	server->active_task = task;
	stw_task_activate(task);

	/* Re-layout */
	stw_layout_arrange_all(server);

	/* Restore focus (TASK-2) */
	if (server->config->task_restore_focus && task->last_focused &&
			task->last_focused->mapped) {
		stw_server_focus_view(server, task->last_focused);
	} else {
		/* Focus the first mapped view in the task */
		struct stw_view *view;
		wl_list_for_each(view, &task->views, task_link) {
			if (view->mapped) {
				stw_server_focus_view(server, view);
				break;
			}
		}
	}
}

void stw_task_switch_next(struct stw_server *server) {
	if (wl_list_empty(&server->tasks)) return;

	struct stw_task *current = server->active_task;
	if (!current) {
		/* Activate first task */
		struct stw_task *first =
			wl_container_of(server->tasks.next, first, link);
		stw_task_switch_to(server, first);
		return;
	}

	/* Find next non-archived task */
	struct stw_task *task = current;
	do {
		if (task->link.next == &server->tasks) {
			task = wl_container_of(server->tasks.next, task, link);
		} else {
			task = wl_container_of(task->link.next, task, link);
		}
		if (!task->archived) {
			stw_task_switch_to(server, task);
			return;
		}
	} while (task != current);
}

void stw_task_switch_prev(struct stw_server *server) {
	if (wl_list_empty(&server->tasks)) return;

	struct stw_task *current = server->active_task;
	if (!current) {
		struct stw_task *last =
			wl_container_of(server->tasks.prev, last, link);
		stw_task_switch_to(server, last);
		return;
	}

	struct stw_task *task = current;
	do {
		if (task->link.prev == &server->tasks) {
			task = wl_container_of(server->tasks.prev, task, link);
		} else {
			task = wl_container_of(task->link.prev, task, link);
		}
		if (!task->archived) {
			stw_task_switch_to(server, task);
			return;
		}
	} while (task != current);
}

void stw_task_switch_by_index(struct stw_server *server, int index) {
	int i = 0;
	struct stw_task *task;
	wl_list_for_each(task, &server->tasks, link) {
		if (!task->archived) {
			if (i == index) {
				stw_task_switch_to(server, task);
				return;
			}
			i++;
		}
	}
	wlr_log(WLR_DEBUG, "No task at index %d", index);
}

/* ─── Task archival ────────────────────────────────────────────── */

void stw_task_archive(struct stw_task *task) {
	task->archived = true;
	if (task->active) {
		stw_task_deactivate(task);
		stw_task_switch_next(task->server);
	}
	wlr_log(WLR_INFO, "Task archived: id=%u name='%s'",
		task->id, task->name);
}

void stw_task_unarchive(struct stw_task *task) {
	task->archived = false;
	wlr_log(WLR_INFO, "Task unarchived: id=%u name='%s'",
		task->id, task->name);
}

/* ─── Task ordering ────────────────────────────────────────────── */

void stw_task_reorder(struct stw_task *task, int new_order) {
	task->order = new_order;
	/* TODO: re-sort task list by order */
}

/* ─── Workspace management ─────────────────────────────────────── */

void stw_task_switch_workspace(struct stw_task *task, int workspace_idx) {
	if (workspace_idx < 0 || workspace_idx >= task->workspace_count) {
		return;
	}
	if (task->active_workspace == workspace_idx) {
		return;
	}

	wlr_log(WLR_DEBUG, "Task '%s': switching to workspace %d",
		task->name, workspace_idx);

	task->active_workspace = workspace_idx;

	/* Show/hide views based on workspace */
	struct stw_view *view;
	wl_list_for_each(view, &task->views, task_link) {
		if (view->is_global) continue;
		if (view->workspace_idx == workspace_idx) {
			stw_view_set_visible(view, task->active);
		} else {
			stw_view_set_visible(view, false);
		}
	}

	stw_layout_arrange_all(task->server);
}

/* ─── Pinned apps ──────────────────────────────────────────────── */

void stw_task_add_pinned_app(struct stw_task *task, const char *desktop_id,
		const char *exec, const char *name) {
	struct stw_pinned_app *app = calloc(1, sizeof(*app));
	if (!app) return;

	app->desktop_id = desktop_id ? strdup(desktop_id) : NULL;
	app->exec = exec ? strdup(exec) : NULL;
	app->name = name ? strdup(name) : NULL;
	wl_list_insert(task->pinned_apps.prev, &app->link);
}

void stw_task_remove_pinned_app(struct stw_task *task,
		const char *desktop_id) {
	struct stw_pinned_app *app, *tmp;
	wl_list_for_each_safe(app, tmp, &task->pinned_apps, link) {
		if (desktop_id && app->desktop_id &&
				strcmp(app->desktop_id, desktop_id) == 0) {
			wl_list_remove(&app->link);
			free(app->desktop_id);
			free(app->exec);
			free(app->name);
			free(app);
			return;
		}
	}
}

/* ─── Query ────────────────────────────────────────────────────── */

struct stw_task *stw_task_find_by_id(struct stw_server *server, uint32_t id) {
	struct stw_task *task;
	wl_list_for_each(task, &server->tasks, link) {
		if (task->id == id) return task;
	}
	return NULL;
}

struct stw_task *stw_task_find_by_name(struct stw_server *server,
		const char *name) {
	struct stw_task *task;
	wl_list_for_each(task, &server->tasks, link) {
		if (strcmp(task->name, name) == 0) return task;
	}
	return NULL;
}

int stw_task_count_views(struct stw_task *task) {
	int count = 0;
	struct stw_view *view;
	wl_list_for_each(view, &task->views, task_link) {
		if (view->mapped) count++;
	}
	return count;
}

int stw_task_get_index(struct stw_task *task) {
	int i = 0;
	struct stw_task *t;
	wl_list_for_each(t, &task->server->tasks, link) {
		if (!t->archived) {
			if (t == task) return i;
			i++;
		}
	}
	return -1;
}

/* ─── ADHD-friendly: task timer ────────────────────────────────── */

uint64_t stw_task_get_timer_seconds(struct stw_task *task) {
	uint64_t total = task->timer_total_secs;
	if (task->timer_switch_in > 0) {
		total += (uint64_t)difftime(time(NULL), task->timer_switch_in);
	}
	return total;
}

/* ─── ADHD-friendly: quick notes ───────────────────────────────── */

void stw_task_add_note(struct stw_task *task, const char *text) {
	if (!task || !text) return;

	struct stw_quick_note *note = calloc(1, sizeof(*note));
	if (!note) return;

	note->text = strdup(text);
	note->created_at = time(NULL);
	wl_list_insert(task->notes.prev, &note->link);

	wlr_log(WLR_DEBUG, "Note added to task '%s': %.40s%s",
		task->name, text, strlen(text) > 40 ? "..." : "");
}

void stw_task_set_sticky(struct stw_task *task, const char *text) {
	if (!task) return;
	free(task->sticky_note);
	task->sticky_note = text ? strdup(text) : NULL;
}
