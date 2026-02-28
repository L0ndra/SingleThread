/*
 * SingleThread - Task-Centric Wayland Compositor
 * view.c - Window (view) management
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "view.h"
#include "server.h"
#include "task.h"
#include "layout.h"
#include "rules.h"

/* ─── Internal listener callbacks (declared for server.c) ──────── */

void stw_view_handle_map(struct wl_listener *listener, void *data) {
	struct stw_view *view = wl_container_of(listener, view, map);
	struct stw_server *server = view->server;
	(void)data;

	view->mapped = true;

	/* Update cached identity from the toplevel */
	if (view->type == STW_VIEW_XDG_TOPLEVEL) {
		free(view->app_id);
		view->app_id = view->xdg_toplevel->app_id ?
			strdup(view->xdg_toplevel->app_id) : NULL;
		free(view->title);
		view->title = view->xdg_toplevel->title ?
			strdup(view->xdg_toplevel->title) : NULL;
	}

	/* Try to resolve parent for transient windows (TASK-4) */
	if (view->type == STW_VIEW_XDG_TOPLEVEL &&
			view->xdg_toplevel->parent) {
		/* Find parent view */
		struct stw_view *v;
		wl_list_for_each(v, &server->views, link) {
			if (v->type == STW_VIEW_XDG_TOPLEVEL &&
					v->xdg_toplevel == view->xdg_toplevel->parent) {
				view->parent = v;
				wl_list_insert(&v->children, &view->child_link);
				break;
			}
		}
	}

	/* Apply rules engine (RULE-1, RULE-2, RULE-3) */
	bool rule_matched = stw_rules_apply(server, view);

	/* Default assignment: active task (TASK-3) */
	if (!rule_matched && !view->task) {
		/* If parent exists, inherit task (TASK-4) */
		if (view->parent && view->parent->task) {
			stw_view_assign_task(view, view->parent->task,
				STW_ASSIGN_DEFAULT);
			wlr_log(WLR_DEBUG, "View '%s' inherits task '%s' from parent",
				view->app_id ? view->app_id : "(null)",
				view->parent->task->name);
		} else if (server->active_task) {
			stw_view_assign_task(view, server->active_task,
				STW_ASSIGN_DEFAULT);
			wlr_log(WLR_DEBUG, "View '%s' assigned to active task '%s'",
				view->app_id ? view->app_id : "(null)",
				server->active_task->name);
		}
	}

	/* Determine visibility based on task */
	if (view->is_global || (view->task && view->task == server->active_task)) {
		stw_view_set_visible(view, true);
	} else {
		stw_view_set_visible(view, false);
	}

	/* Focus the new view if visible */
	if (view->is_global || (view->task && view->task == server->active_task)) {
		stw_server_focus_view(server, view);
	}

	/* Trigger layout */
	stw_layout_arrange_all(server);
}

void stw_view_handle_unmap(struct wl_listener *listener, void *data) {
	struct stw_view *view = wl_container_of(listener, view, unmap);
	(void)data;

	view->mapped = false;

	/* Update task's last focused if this was it */
	if (view->task && view->task->last_focused == view) {
		view->task->last_focused = NULL;
		/* Find another view to be last_focused */
		struct stw_view *v;
		wl_list_for_each(v, &view->task->views, task_link) {
			if (v != view && v->mapped) {
				view->task->last_focused = v;
				break;
			}
		}
	}

	/* Re-layout */
	stw_layout_arrange_all(view->server);
}

void stw_view_handle_destroy(struct wl_listener *listener, void *data) {
	struct stw_view *view = wl_container_of(listener, view, destroy);
	(void)data;

	/* Remove listeners */
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->set_title.link);
	wl_list_remove(&view->set_app_id.link);

	/* Remove from task */
	if (view->task) {
		wl_list_remove(&view->task_link);
	}

	/* Remove from parent's children list */
	if (view->parent) {
		wl_list_remove(&view->child_link);
	}

	/* Reparent children */
	struct stw_view *child, *child_tmp;
	wl_list_for_each_safe(child, child_tmp, &view->children, child_link) {
		child->parent = NULL;
		wl_list_remove(&child->child_link);
		wl_list_init(&child->child_link);
	}

	/* Remove from server list */
	wl_list_remove(&view->link);

	/* If server was grabbing this view, reset */
	if (view->server->grabbed_view == view) {
		view->server->cursor_mode = STW_CURSOR_PASSTHROUGH;
		view->server->grabbed_view = NULL;
	}

	free(view->app_id);
	free(view->title);
	free(view);
}

void stw_view_handle_request_move(struct wl_listener *listener, void *data) {
	struct stw_view *view = wl_container_of(listener, view, request_move);
	(void)data;
	stw_cursor_begin_move(view->server, view);
}

void stw_view_handle_request_resize(struct wl_listener *listener, void *data) {
	struct stw_view *view = wl_container_of(listener, view, request_resize);
	struct wlr_xdg_toplevel_resize_event *event = data;
	stw_cursor_begin_resize(view->server, view, event->edges);
}

void stw_view_handle_request_maximize(struct wl_listener *listener,
		void *data) {
	struct stw_view *view = wl_container_of(listener, view, request_maximize);
	(void)data;

	if (view->xdg_toplevel) {
		/* Toggle maximized state - in tiling WM, we just set tiled */
		if (view->state == STW_VIEW_FLOATING) {
			stw_view_set_state(view, STW_VIEW_MAXIMIZED);
		} else {
			wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
		}
	}
}

void stw_view_handle_request_fullscreen(struct wl_listener *listener,
		void *data) {
	struct stw_view *view =
		wl_container_of(listener, view, request_fullscreen);
	(void)data;
	stw_view_toggle_fullscreen(view);
}

void stw_view_handle_set_title(struct wl_listener *listener, void *data) {
	struct stw_view *view = wl_container_of(listener, view, set_title);
	(void)data;
	free(view->title);
	if (view->type == STW_VIEW_XDG_TOPLEVEL && view->xdg_toplevel->title) {
		view->title = strdup(view->xdg_toplevel->title);
	} else {
		view->title = NULL;
	}
}

void stw_view_handle_set_app_id(struct wl_listener *listener, void *data) {
	struct stw_view *view = wl_container_of(listener, view, set_app_id);
	(void)data;
	free(view->app_id);
	if (view->type == STW_VIEW_XDG_TOPLEVEL && view->xdg_toplevel->app_id) {
		view->app_id = strdup(view->xdg_toplevel->app_id);
	} else {
		view->app_id = NULL;
	}
}

/* ─── View operations ──────────────────────────────────────────── */

void stw_view_focus(struct stw_view *view) {
	if (!view) return;
	stw_server_focus_view(view->server, view);
}

void stw_view_close(struct stw_view *view) {
	if (!view) return;
	switch (view->type) {
	case STW_VIEW_XDG_TOPLEVEL:
		wlr_xdg_toplevel_send_close(view->xdg_toplevel);
		break;
#if STW_HAS_XWAYLAND
	case STW_VIEW_XWAYLAND:
		wlr_xwayland_surface_close(view->xwayland_surface);
		break;
#endif
	}
}

void stw_view_set_state(struct stw_view *view, enum stw_view_state state) {
	if (view->state == state) return;

	enum stw_view_state old_state = view->state;

	/* Save geometry before fullscreen/maximize */
	if ((state == STW_VIEW_FULLSCREEN || state == STW_VIEW_MAXIMIZED) &&
			old_state != STW_VIEW_FULLSCREEN &&
			old_state != STW_VIEW_MAXIMIZED) {
		stw_view_get_geometry(view, &view->saved_geometry);
	}

	/* Save floating geometry when entering tiled */
	if (state == STW_VIEW_TILED && old_state == STW_VIEW_FLOATING) {
		stw_view_get_geometry(view, &view->floating_geometry);
	}

	view->state = state;

	/* Move to appropriate scene layer */
	struct wlr_scene_tree *target;
	switch (state) {
	case STW_VIEW_FULLSCREEN:
		target = view->server->scene_fullscreen;
		break;
	case STW_VIEW_FLOATING:
		target = view->is_global ?
			view->server->scene_global :
			view->server->scene_floating;
		break;
	default:
		target = view->is_global ?
			view->server->scene_global :
			view->server->scene_tiled;
		break;
	}
	wlr_scene_node_reparent(&view->scene_tree->node, target);

	/* Apply fullscreen state to toplevel */
	if (view->type == STW_VIEW_XDG_TOPLEVEL) {
		wlr_xdg_toplevel_set_fullscreen(view->xdg_toplevel,
			state == STW_VIEW_FULLSCREEN);
	}

	/* Restore geometry when leaving fullscreen/maximize */
	if (old_state == STW_VIEW_FULLSCREEN || old_state == STW_VIEW_MAXIMIZED) {
		if (state == STW_VIEW_FLOATING && view->floating_geometry.width > 0) {
			stw_view_set_position(view,
				view->floating_geometry.x,
				view->floating_geometry.y);
			stw_view_set_size(view,
				view->floating_geometry.width,
				view->floating_geometry.height);
		} else if (view->saved_geometry.width > 0) {
			stw_view_set_position(view,
				view->saved_geometry.x,
				view->saved_geometry.y);
			stw_view_set_size(view,
				view->saved_geometry.width,
				view->saved_geometry.height);
		}
	}

	/* Restore floating geometry when leaving tiled */
	if (old_state == STW_VIEW_TILED && state == STW_VIEW_FLOATING) {
		if (view->floating_geometry.width > 0) {
			stw_view_set_position(view,
				view->floating_geometry.x,
				view->floating_geometry.y);
			stw_view_set_size(view,
				view->floating_geometry.width,
				view->floating_geometry.height);
		}
	}

	stw_layout_arrange_all(view->server);
}

void stw_view_set_floating(struct stw_view *view, bool floating) {
	if (floating) {
		stw_view_set_state(view, STW_VIEW_FLOATING);
	} else {
		stw_view_set_state(view, STW_VIEW_TILED);
	}
}

void stw_view_toggle_floating(struct stw_view *view) {
	if (view->state == STW_VIEW_FLOATING) {
		stw_view_set_state(view, STW_VIEW_TILED);
	} else {
		stw_view_set_state(view, STW_VIEW_FLOATING);
	}
}

void stw_view_set_fullscreen(struct stw_view *view, bool fullscreen) {
	if (fullscreen) {
		stw_view_set_state(view, STW_VIEW_FULLSCREEN);
	} else {
		stw_view_set_state(view, view->floating_geometry.width > 0 ?
			STW_VIEW_FLOATING : STW_VIEW_TILED);
	}
}

void stw_view_toggle_fullscreen(struct stw_view *view) {
	stw_view_set_fullscreen(view, view->state != STW_VIEW_FULLSCREEN);
}

/* ─── Task assignment ──────────────────────────────────────────── */

void stw_view_assign_task(struct stw_view *view, struct stw_task *task,
		enum stw_assignment_source source) {
	if (!view || !task) return;

	/* Don't override manual assignment with rule/default */
	if (view->assignment_source == STW_ASSIGN_MANUAL &&
			source != STW_ASSIGN_MANUAL) {
		return;
	}

	/* Remove from current task */
	if (view->task) {
		wl_list_remove(&view->task_link);
		if (view->task->last_focused == view) {
			view->task->last_focused = NULL;
		}
	}

	/* Assign to new task */
	view->task = task;
	view->assignment_source = source;
	wl_list_insert(&task->views, &view->task_link);

	/* Update visibility */
	struct stw_server *server = view->server;
	if (view->is_global || task == server->active_task) {
		stw_view_set_visible(view, true);
	} else {
		stw_view_set_visible(view, false);
	}

	wlr_log(WLR_DEBUG, "View '%s' assigned to task '%s' (source=%d)",
		view->app_id ? view->app_id : "(null)", task->name, source);

	stw_layout_arrange_all(server);
}

void stw_view_set_global(struct stw_view *view, bool global) {
	if (view->is_global == global) return;

	view->is_global = global;

	/* Move to global scene layer or back */
	struct wlr_scene_tree *target;
	if (global) {
		target = view->server->scene_global;
	} else if (view->state == STW_VIEW_FLOATING) {
		target = view->server->scene_floating;
	} else {
		target = view->server->scene_tiled;
	}
	wlr_scene_node_reparent(&view->scene_tree->node, target);

	/* Always visible when global */
	if (global) {
		stw_view_set_visible(view, true);
	} else {
		/* Check if view's task is active */
		stw_view_set_visible(view,
			view->task == view->server->active_task);
	}

	wlr_log(WLR_DEBUG, "View '%s' global=%s",
		view->app_id ? view->app_id : "(null)",
		global ? "true" : "false");

	stw_layout_arrange_all(view->server);
}

void stw_view_toggle_global(struct stw_view *view) {
	stw_view_set_global(view, !view->is_global);
}

/* ─── Geometry ─────────────────────────────────────────────────── */

void stw_view_set_position(struct stw_view *view, int x, int y) {
	wlr_scene_node_set_position(&view->scene_tree->node, x, y);
}

void stw_view_set_size(struct stw_view *view, int width, int height) {
	if (width <= 0 || height <= 0) return;

	switch (view->type) {
	case STW_VIEW_XDG_TOPLEVEL:
		wlr_xdg_toplevel_set_size(view->xdg_toplevel, width, height);
		break;
#if STW_HAS_XWAYLAND
	case STW_VIEW_XWAYLAND:
		wlr_xwayland_surface_configure(view->xwayland_surface,
			view->scene_tree->node.x, view->scene_tree->node.y,
			width, height);
		break;
#endif
	}
}

void stw_view_get_geometry(struct stw_view *view, struct wlr_box *box) {
	switch (view->type) {
	case STW_VIEW_XDG_TOPLEVEL:
		wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, box);
		box->x = view->scene_tree->node.x;
		box->y = view->scene_tree->node.y;
		break;
#if STW_HAS_XWAYLAND
	case STW_VIEW_XWAYLAND:
		box->x = view->xwayland_surface->x;
		box->y = view->xwayland_surface->y;
		box->width = view->xwayland_surface->width;
		box->height = view->xwayland_surface->height;
		break;
#endif
	}
}

/* ─── Identity helpers ─────────────────────────────────────────── */

const char *stw_view_get_app_id(struct stw_view *view) {
	return view->app_id;
}

const char *stw_view_get_title(struct stw_view *view) {
	return view->title;
}

/* ─── Visibility ───────────────────────────────────────────────── */

void stw_view_set_visible(struct stw_view *view, bool visible) {
	wlr_scene_node_set_enabled(&view->scene_tree->node, visible);
}
