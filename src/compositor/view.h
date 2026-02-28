/*
 * SingleThread - Task-Centric Wayland Compositor
 * view.h - Window (view) management
 */
#ifndef STW_VIEW_H
#define STW_VIEW_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_scene.h>

struct stw_server;
struct stw_task;

/* ─── View type ────────────────────────────────────────────────── */
enum stw_view_type {
	STW_VIEW_XDG_TOPLEVEL,
#if STW_HAS_XWAYLAND
	STW_VIEW_XWAYLAND,
#endif
};

/* ─── View state ───────────────────────────────────────────────── */
enum stw_view_state {
	STW_VIEW_TILED,
	STW_VIEW_FLOATING,
	STW_VIEW_FULLSCREEN,
	STW_VIEW_MAXIMIZED,
};

/* ─── Manual override for rule precedence ──────────────────────── */
enum stw_assignment_source {
	STW_ASSIGN_DEFAULT,    /* default: active task */
	STW_ASSIGN_RULE,       /* matched by a rule */
	STW_ASSIGN_MANUAL,     /* explicit user override */
};

/* ─── View (a managed window) ──────────────────────────────────── */
struct stw_view {
	struct wl_list link;           /* stw_server.views */
	struct wl_list task_link;      /* stw_task.views */
	struct stw_server *server;

	/* Type and surface */
	enum stw_view_type type;
	union {
		struct wlr_xdg_toplevel *xdg_toplevel;
#if STW_HAS_XWAYLAND
		struct wlr_xwayland_surface *xwayland_surface;
#endif
	};

	/* Scene graph node */
	struct wlr_scene_tree *scene_tree;

	/* Border decorations (4 rects: top, bottom, left, right) */
	struct wlr_scene_rect *border[4];

	/* State */
	enum stw_view_state state;
	bool is_global;                /* visible across all tasks */
	enum stw_assignment_source assignment_source;

	/* Task assignment */
	struct stw_task *task;
	int workspace_idx;             /* workspace within task (0-based) */

	/* Geometry (for floating/tiled restore) */
	struct wlr_box saved_geometry; /* saved before fullscreen/maximize */
	struct wlr_box floating_geometry;

	/* Identification (cached for rules matching) */
	char *app_id;                  /* Wayland app_id or XWayland class */
	char *title;
	pid_t pid;

	/* Parent tracking (for transient windows) */
	struct stw_view *parent;
	struct wl_list children;       /* stw_view.child_link */
	struct wl_list child_link;

	/* Listeners */
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
	struct wl_listener set_app_id;

	/* Mapped state */
	bool mapped;

	/* Focus tracking */
	uint64_t last_focus_time;      /* for restoring focus on task switch */
};

/* Internal listener callbacks (used by server.c) */
void stw_view_handle_map(struct wl_listener *listener, void *data);
void stw_view_handle_unmap(struct wl_listener *listener, void *data);
void stw_view_handle_destroy(struct wl_listener *listener, void *data);
void stw_view_handle_request_move(struct wl_listener *listener, void *data);
void stw_view_handle_request_resize(struct wl_listener *listener, void *data);
void stw_view_handle_request_maximize(struct wl_listener *listener, void *data);
void stw_view_handle_request_fullscreen(struct wl_listener *listener, void *data);
void stw_view_handle_set_title(struct wl_listener *listener, void *data);
void stw_view_handle_set_app_id(struct wl_listener *listener, void *data);
void stw_view_handle_commit(struct wl_listener *listener, void *data);

/* View operations */

/* View operations */
void stw_view_focus(struct stw_view *view);
void stw_view_close(struct stw_view *view);
void stw_view_set_state(struct stw_view *view, enum stw_view_state state);
void stw_view_set_floating(struct stw_view *view, bool floating);
void stw_view_toggle_floating(struct stw_view *view);
void stw_view_set_fullscreen(struct stw_view *view, bool fullscreen);
void stw_view_toggle_fullscreen(struct stw_view *view);

/* Task assignment */
void stw_view_assign_task(struct stw_view *view, struct stw_task *task,
	enum stw_assignment_source source);
void stw_view_set_global(struct stw_view *view, bool global);
void stw_view_toggle_global(struct stw_view *view);

/* Geometry */
void stw_view_set_position(struct stw_view *view, int x, int y);
void stw_view_set_size(struct stw_view *view, int width, int height);
void stw_view_get_geometry(struct stw_view *view, struct wlr_box *box);

/* Identity helpers */
const char *stw_view_get_app_id(struct stw_view *view);
const char *stw_view_get_title(struct stw_view *view);

/* Visibility (task-scoped) */
void stw_view_set_visible(struct stw_view *view, bool visible);

/* Border decorations */
void stw_view_update_border_color(struct stw_view *view, bool focused);

#endif /* STW_VIEW_H */
