/*
 * SingleThread - Task-Centric Wayland Compositor
 * xwayland.c - XWayland surface management
 */
#define _POSIX_C_SOURCE 200809L
#include "config.h"

#if STW_HAS_XWAYLAND

#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "xwayland.h"
#include "server.h"
#include "view.h"
#include "task.h"
#include "rules.h"
#include "layout.h"

/* ─── XWayland view listeners ──────────────────────────────────── */

static void xwayland_map(struct wl_listener *listener, void *data) {
	struct stw_view *view = wl_container_of(listener, view, map);
	struct stw_server *server = view->server;
	struct wlr_xwayland_surface *xsurface = view->xwayland_surface;
	(void)data;

	view->mapped = true;

	/* Cache identity */
	free(view->app_id);
	view->app_id = xsurface->class ? strdup(xsurface->class) : NULL;
	free(view->title);
	view->title = xsurface->title ? strdup(xsurface->title) : NULL;
	view->pid = xsurface->pid;

	/* Create scene node */
	view->scene_tree = wlr_scene_subsurface_tree_create(
		server->scene_tiled, xsurface->surface);
	if (view->scene_tree) {
		view->scene_tree->node.data = view;
	}

	/* Check if this is a dialog/transient */
	if (xsurface->parent) {
		struct stw_view *v;
		wl_list_for_each(v, &server->views, link) {
			if (v->type == STW_VIEW_XWAYLAND &&
					v->xwayland_surface == xsurface->parent) {
				view->parent = v;
				wl_list_insert(&v->children, &view->child_link);
				break;
			}
		}
	}

	/* Determine if it should be floating */
	if (xsurface->override_redirect ||
			(xsurface->size_hints &&
			 xsurface->size_hints->min_width > 0 &&
			 xsurface->size_hints->min_width ==
			 xsurface->size_hints->max_width)) {
		view->state = STW_VIEW_FLOATING;
		wlr_scene_node_reparent(&view->scene_tree->node,
			server->scene_floating);
	}

	/* Apply rules */
	bool rule_matched = stw_rules_apply(server, view);

	/* Default task assignment */
	if (!rule_matched && !view->task) {
		if (view->parent && view->parent->task) {
			stw_view_assign_task(view, view->parent->task,
				STW_ASSIGN_DEFAULT);
		} else if (server->active_task) {
			stw_view_assign_task(view, server->active_task,
				STW_ASSIGN_DEFAULT);
		}
	}

	/* Set initial position for floating windows */
	if (view->state == STW_VIEW_FLOATING) {
		stw_view_set_position(view, xsurface->x, xsurface->y);
		view->floating_geometry.x = xsurface->x;
		view->floating_geometry.y = xsurface->y;
		view->floating_geometry.width = xsurface->width;
		view->floating_geometry.height = xsurface->height;
	}

	/* Visibility */
	if (view->is_global || (view->task && view->task == server->active_task)) {
		stw_view_set_visible(view, true);
		stw_server_focus_view(server, view);
	} else {
		stw_view_set_visible(view, false);
	}

	stw_layout_arrange_all(server);

	wlr_log(WLR_DEBUG, "XWayland surface mapped: class=%s title=%s pid=%d",
		view->app_id ? view->app_id : "(null)",
		view->title ? view->title : "(null)",
		view->pid);
}

static void xwayland_unmap(struct wl_listener *listener, void *data) {
	struct stw_view *view = wl_container_of(listener, view, unmap);
	(void)data;

	view->mapped = false;

	if (view->task && view->task->last_focused == view) {
		view->task->last_focused = NULL;
	}

	if (view->scene_tree) {
		wlr_scene_node_destroy(&view->scene_tree->node);
		view->scene_tree = NULL;
	}

	stw_layout_arrange_all(view->server);
}

static void xwayland_destroy(struct wl_listener *listener, void *data) {
	struct stw_view *view = wl_container_of(listener, view, destroy);
	(void)data;

	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->set_title.link);

	if (view->task) {
		wl_list_remove(&view->task_link);
	}
	if (view->parent) {
		wl_list_remove(&view->child_link);
	}

	struct stw_view *child, *child_tmp;
	wl_list_for_each_safe(child, child_tmp, &view->children, child_link) {
		child->parent = NULL;
		wl_list_remove(&child->child_link);
		wl_list_init(&child->child_link);
	}

	wl_list_remove(&view->link);

	if (view->server->grabbed_view == view) {
		view->server->cursor_mode = STW_CURSOR_PASSTHROUGH;
		view->server->grabbed_view = NULL;
	}

	free(view->app_id);
	free(view->title);
	free(view);
}

static void xwayland_set_title(struct wl_listener *listener, void *data) {
	struct stw_view *view = wl_container_of(listener, view, set_title);
	(void)data;
	free(view->title);
	view->title = view->xwayland_surface->title ?
		strdup(view->xwayland_surface->title) : NULL;
}

/* ─── New XWayland surface ─────────────────────────────────────── */

void stw_xwayland_handle_new_surface(struct stw_server *server,
		struct wlr_xwayland_surface *xsurface) {
	struct stw_view *view = calloc(1, sizeof(*view));
	if (!view) {
		wlr_log(WLR_ERROR, "Failed to allocate XWayland view");
		return;
	}

	view->server = server;
	view->type = STW_VIEW_XWAYLAND;
	view->xwayland_surface = xsurface;
	view->state = STW_VIEW_TILED;
	view->workspace_idx = 0;
	wl_list_init(&view->children);
	wl_list_init(&view->child_link);

	/* Cache identity */
	view->app_id = xsurface->class ? strdup(xsurface->class) : NULL;
	view->title = xsurface->title ? strdup(xsurface->title) : NULL;
	view->pid = xsurface->pid;

	/* Listeners */
	view->map.notify = xwayland_map;
	wl_signal_add(&xsurface->surface->events.map, &view->map);

	view->unmap.notify = xwayland_unmap;
	wl_signal_add(&xsurface->surface->events.unmap, &view->unmap);

	view->destroy.notify = xwayland_destroy;
	wl_signal_add(&xsurface->events.destroy, &view->destroy);

	view->set_title.notify = xwayland_set_title;
	wl_signal_add(&xsurface->events.set_title, &view->set_title);

	wl_list_insert(&server->views, &view->link);
}

#endif /* STW_HAS_XWAYLAND */
