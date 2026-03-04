/*
 * SingleThread - Task-Centric Wayland Compositor
 * task.h - Task (context) system
 */
#ifndef STW_TASK_H
#define STW_TASK_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server-core.h>

struct stw_server;
struct stw_view;
struct stw_task_timer;

/* ─── Workspace (sub-container within a task) ──────────────────── */
#define STW_MAX_WORKSPACES 10

struct stw_workspace {
	int index;
	char *name;
	struct wl_list views; /* views assigned to this workspace (not used
	                         directly; views track workspace_idx) */

	/* Layout state */
	int master_count;
	double master_ratio;
};

/* ─── Pinned app entry ─────────────────────────────────────────── */
struct stw_pinned_app {
	struct wl_list link; /* stw_task.pinned_apps */
	char *desktop_id;    /* e.g. "firefox.desktop" */
	char *exec;          /* fallback exec command */
	char *name;          /* display name */
};

/* ─── Task (context container) ─────────────────────────────────── */
struct stw_task {
	struct wl_list link;       /* stw_server.tasks */
	struct stw_server *server;

	uint32_t id;
	char *name;
	char *icon;                /* optional icon name */
	uint32_t color;            /* optional color (RGBA) */
	int order;                 /* display order */

	/* State */
	bool active;
	bool archived;

	/* Views assigned to this task */
	struct wl_list views;      /* stw_view.task_link */

	/* Last focused view (for focus restore on task switch) */
	struct stw_view *last_focused;

	/* Workspaces within this task */
	struct stw_workspace workspaces[STW_MAX_WORKSPACES];
	int workspace_count;
	int active_workspace;

	/* Per-task pinned applications */
	struct wl_list pinned_apps; /* stw_pinned_app.link */

	/* Layout preference override (NULL = use global default) */
	char *layout_name;

	/* ─── ADHD-friendly features ──────────────────────────────── */

	/* Per-task accent color (auto-assigned from palette, RGBA) */
	uint32_t accent_color;

	/* Task timer - tracks cumulative time on this task */
	time_t timer_switch_in;    /* when we last switched TO this task */
	uint64_t timer_total_secs; /* cumulative seconds spent */

	/* Quick notes attached to this task */
	struct wl_list notes;      /* stw_quick_note.link */

	/* "What was I doing?" reminder text */
	char *sticky_note;
};

/* Task lifecycle */
struct stw_task *stw_task_create(struct stw_server *server, const char *name);
void stw_task_destroy(struct stw_task *task);
void stw_task_rename(struct stw_task *task, const char *name);

/* Task switching */
void stw_task_activate(struct stw_task *task);
void stw_task_deactivate(struct stw_task *task);
void stw_task_switch_to(struct stw_server *server, struct stw_task *task);
void stw_task_switch_next(struct stw_server *server);
void stw_task_switch_prev(struct stw_server *server);
void stw_task_switch_by_index(struct stw_server *server, int index);

/* Task archival */
void stw_task_archive(struct stw_task *task);
void stw_task_unarchive(struct stw_task *task);

/* Task ordering */
void stw_task_reorder(struct stw_task *task, int new_order);

/* Workspace management */
void stw_task_switch_workspace(struct stw_task *task, int workspace_idx);

/* Pinned apps */
void stw_task_add_pinned_app(struct stw_task *task, const char *desktop_id,
	const char *exec, const char *name);
void stw_task_remove_pinned_app(struct stw_task *task, const char *desktop_id);

/* Query */
struct stw_task *stw_task_find_by_id(struct stw_server *server, uint32_t id);
struct stw_task *stw_task_find_by_name(struct stw_server *server, const char *name);
int stw_task_count_views(struct stw_task *task);
int stw_task_get_index(struct stw_task *task);

/* ADHD-friendly: task timer */
uint64_t stw_task_get_timer_seconds(struct stw_task *task);

/* ADHD-friendly: quick notes */
void stw_task_add_note(struct stw_task *task, const char *text);
void stw_task_set_sticky(struct stw_task *task, const char *text);

#endif /* STW_TASK_H */
