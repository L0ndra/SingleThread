/*
 * SingleThread - Task-Centric Wayland Compositor
 * server.c - Core server initialization and lifecycle
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#if STW_HAS_XWAYLAND
#include <wlr/xwayland.h>
#endif

#include "server.h"
#include "output.h"
#include "input.h"
#include "view.h"
#include "task.h"
#include "layout.h"
#include "rules.h"
#include "ipc.h"
#include "session.h"
#include "stw_config.h"

/* ─── XDG shell handlers ──────────────────────────────────────── */

static void handle_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *toplevel = data;

	/* Allocate view */
	struct stw_view *view = calloc(1, sizeof(*view));
	if (!view) {
		wlr_log(WLR_ERROR, "Failed to allocate view");
		return;
	}

	view->server = server;
	view->type = STW_VIEW_XDG_TOPLEVEL;
	view->xdg_toplevel = toplevel;
	view->state = STW_VIEW_TILED;
	view->workspace_idx = 0;
	wl_list_init(&view->children);
	wl_list_init(&view->child_link);

	/* Create scene tree node */
	view->scene_tree = wlr_scene_xdg_surface_create(
		server->scene_tiled, toplevel->base);
	if (!view->scene_tree) {
		wlr_log(WLR_ERROR, "Failed to create scene tree for view");
		free(view);
		return;
	}
	view->scene_tree->node.data = view;

	/* Cache identity info */
	if (toplevel->app_id) {
		view->app_id = strdup(toplevel->app_id);
	}
	if (toplevel->title) {
		view->title = strdup(toplevel->title);
	}

	/* Check for parent (transient) */
	if (toplevel->parent) {
		struct wlr_scene_tree *parent_tree =
			toplevel->parent->base->surface->data ?
			NULL : NULL; /* will be resolved on map */
		(void)parent_tree;
	}

	/* Set up listeners */
	view->map.notify = stw_view_handle_map;
	wl_signal_add(&toplevel->base->surface->events.map, &view->map);

	view->unmap.notify = stw_view_handle_unmap;
	wl_signal_add(&toplevel->base->surface->events.unmap, &view->unmap);

	view->destroy.notify = stw_view_handle_destroy;
	wl_signal_add(&toplevel->base->events.destroy, &view->destroy);

	view->request_move.notify = stw_view_handle_request_move;
	wl_signal_add(&toplevel->events.request_move, &view->request_move);

	view->request_resize.notify = stw_view_handle_request_resize;
	wl_signal_add(&toplevel->events.request_resize, &view->request_resize);

	view->request_maximize.notify = stw_view_handle_request_maximize;
	wl_signal_add(&toplevel->events.request_maximize, &view->request_maximize);

	view->request_fullscreen.notify = stw_view_handle_request_fullscreen;
	wl_signal_add(&toplevel->events.request_fullscreen,
		&view->request_fullscreen);

	view->set_title.notify = stw_view_handle_set_title;
	wl_signal_add(&toplevel->events.set_title, &view->set_title);

	view->set_app_id.notify = stw_view_handle_set_app_id;
	wl_signal_add(&toplevel->events.set_app_id, &view->set_app_id);

	/* Add to server's view list */
	wl_list_insert(&server->views, &view->link);

	wlr_log(WLR_DEBUG, "New XDG toplevel: app_id=%s title=%s",
		view->app_id ? view->app_id : "(null)",
		view->title ? view->title : "(null)");
}

static void handle_new_xdg_popup(struct wl_listener *listener, void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, new_xdg_popup);
	struct wlr_xdg_popup *popup = data;

	/* Popups are automatically handled by the scene graph when parented
	 * to an xdg_surface scene tree */
	struct wlr_xdg_surface *parent =
		wlr_xdg_surface_try_from_wlr_surface(popup->parent);
	if (parent) {
		struct wlr_scene_tree *parent_tree = parent->data;
		if (parent_tree) {
			wlr_scene_xdg_surface_create(parent_tree, popup->base);
		}
	}
}

static void handle_xdg_decoration(struct wl_listener *listener, void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, xdg_decoration);
	struct wlr_xdg_toplevel_decoration_v1 *decoration = data;

	/* Prefer server-side decoration */
	wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
		WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void handle_new_layer_surface(struct wl_listener *listener, void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, new_layer_surface);
	struct wlr_layer_surface_v1 *layer_surface = data;

	wlr_log(WLR_DEBUG, "New layer surface: namespace=%s",
		layer_surface->namespace ? layer_surface->namespace : "(null)");

	/* Layer surfaces are placed in the overlay scene tree */
	wlr_scene_layer_surface_v1_create(server->scene_overlay, layer_surface);

	/* If no output specified, use the focused output */
	if (!layer_surface->output) {
		struct stw_output *output = stw_output_get_focused(server);
		if (output) {
			layer_surface->output = output->wlr_output;
		}
	}
}

#if STW_HAS_XWAYLAND
static void handle_xwayland_ready(struct wl_listener *listener, void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, xwayland_ready);
	(void)data;
	wlr_log(WLR_INFO, "XWayland ready");
	/* Set DISPLAY for child processes */
	/* The xwayland instance sets this automatically */
}

static void handle_new_xwayland_surface(struct wl_listener *listener,
		void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, new_xwayland_surface);
	struct wlr_xwayland_surface *xsurface = data;

	wlr_log(WLR_DEBUG, "New XWayland surface: class=%s title=%s",
		xsurface->class ? xsurface->class : "(null)",
		xsurface->title ? xsurface->title : "(null)");

	/* XWayland surface handling - similar to XDG but with X11 properties */
	/* Full implementation in xwayland.c */
	stw_xwayland_handle_new_surface(server, xsurface);
}
#endif

/* ─── Server initialization ───────────────────────────────────── */

bool stw_server_init(struct stw_server *server) {
	/* Create Wayland display */
	server->wl_display = wl_display_create();
	if (!server->wl_display) {
		wlr_log(WLR_ERROR, "Failed to create Wayland display");
		return false;
	}
	server->wl_event_loop = wl_display_get_event_loop(server->wl_display);

	/* Create backend */
	server->backend = wlr_backend_autocreate(
		wl_display_get_event_loop(server->wl_display), NULL);
	if (!server->backend) {
		wlr_log(WLR_ERROR, "Failed to create backend");
		goto err_display;
	}

	/* Create renderer */
	server->renderer = wlr_renderer_autocreate(server->backend);
	if (!server->renderer) {
		wlr_log(WLR_ERROR, "Failed to create renderer");
		goto err_backend;
	}
	wlr_renderer_init_wl_display(server->renderer, server->wl_display);

	/* Create allocator */
	server->allocator = wlr_allocator_autocreate(
		server->backend, server->renderer);
	if (!server->allocator) {
		wlr_log(WLR_ERROR, "Failed to create allocator");
		goto err_renderer;
	}

	/* Create scene graph */
	server->scene = wlr_scene_create();
	if (!server->scene) {
		wlr_log(WLR_ERROR, "Failed to create scene");
		goto err_allocator;
	}

	/* Create scene layers (render order: bottom to top) */
	server->scene_background = wlr_scene_tree_create(&server->scene->tree);
	server->scene_tiled = wlr_scene_tree_create(&server->scene->tree);
	server->scene_floating = wlr_scene_tree_create(&server->scene->tree);
	server->scene_fullscreen = wlr_scene_tree_create(&server->scene->tree);
	server->scene_global = wlr_scene_tree_create(&server->scene->tree);
	server->scene_overlay = wlr_scene_tree_create(&server->scene->tree);

	/* Output layout */
	server->output_layout = wlr_output_layout_create(server->wl_display);
	server->scene_layout = wlr_scene_attach_output_layout(
		server->scene, server->output_layout);

	/* Wayland compositor global */
	server->compositor = wlr_compositor_create(
		server->wl_display, 5, server->renderer);
	server->subcompositor = wlr_subcompositor_create(server->wl_display);

	/* Data device manager (clipboard/DnD) */
	server->data_device_mgr = wlr_data_device_manager_create(
		server->wl_display);

	/* Initialize output list and listener */
	wl_list_init(&server->outputs);
	server->new_output.notify = stw_output_handle_new;
	wl_signal_add(&server->backend->events.new_output, &server->new_output);

	/* XDG shell */
	server->xdg_shell = wlr_xdg_shell_create(server->wl_display, 3);
	server->new_xdg_toplevel.notify = handle_new_xdg_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel,
		&server->new_xdg_toplevel);
	server->new_xdg_popup.notify = handle_new_xdg_popup;
	wl_signal_add(&server->xdg_shell->events.new_popup,
		&server->new_xdg_popup);

	/* Layer shell */
	server->layer_shell = wlr_layer_shell_v1_create(server->wl_display, 4);
	server->new_layer_surface.notify = handle_new_layer_surface;
	wl_signal_add(&server->layer_shell->events.new_surface,
		&server->new_layer_surface);

	/* XDG decoration */
	server->xdg_decoration_mgr =
		wlr_xdg_decoration_manager_v1_create(server->wl_display);
	server->xdg_decoration.notify = handle_xdg_decoration;
	wl_signal_add(&server->xdg_decoration_mgr->events.new_toplevel_decoration,
		&server->xdg_decoration);

	/* Seat (input) */
	server->seat = wlr_seat_create(server->wl_display, "seat0");
	wl_list_init(&server->keyboards);

	/* Cursor */
	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
	server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	server->cursor_mode = STW_CURSOR_PASSTHROUGH;

	/* Cursor listeners */
	server->cursor_motion.notify = stw_cursor_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
	server->cursor_motion_absolute.notify = stw_cursor_motion_absolute;
	wl_signal_add(&server->cursor->events.motion_absolute,
		&server->cursor_motion_absolute);
	server->cursor_button.notify = stw_cursor_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);
	server->cursor_axis.notify = stw_cursor_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
	server->cursor_frame.notify = stw_cursor_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

	/* Seat listeners */
	server->request_set_cursor.notify = stw_cursor_request_set;
	wl_signal_add(&server->seat->events.request_set_cursor,
		&server->request_set_cursor);
	server->request_set_selection.notify = NULL; /* TODO */

	/* Input listener */
	server->new_input.notify = stw_input_handle_new;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);

	/* Initialize view list */
	wl_list_init(&server->views);

	/* Initialize task system */
	wl_list_init(&server->tasks);
	server->next_task_id = 1;

	/* Initialize rules */
	stw_rules_init(server);
	stw_rules_load(server);

	/* Initialize IPC */
	wl_list_init(&server->ipc_clients);
	if (!stw_ipc_init(server)) {
		wlr_log(WLR_ERROR, "Failed to initialize IPC");
		/* Non-fatal: continue without IPC */
	}

#if STW_HAS_XWAYLAND
	/* XWayland */
	server->xwayland = wlr_xwayland_create(server->wl_display,
		server->compositor, true);
	if (server->xwayland) {
		server->xwayland_ready.notify = handle_xwayland_ready;
		wl_signal_add(&server->xwayland->events.ready,
			&server->xwayland_ready);
		server->new_xwayland_surface.notify = handle_new_xwayland_surface;
		wl_signal_add(&server->xwayland->events.new_surface,
			&server->new_xwayland_surface);
		wlr_log(WLR_INFO, "XWayland support enabled");
	} else {
		wlr_log(WLR_ERROR, "Failed to start XWayland");
	}
#endif

	/* Add Wayland socket */
	server->socket = wl_display_add_socket_auto(server->wl_display);
	if (!server->socket) {
		wlr_log(WLR_ERROR, "Failed to create Wayland socket");
		goto err_scene;
	}

	/* Restore session or create default task */
	server->session_path = stw_session_path();
	if (!stw_session_restore(server)) {
		if (server->config->task_create_default) {
			struct stw_task *default_task = stw_task_create(server,
				server->config->task_default_name);
			if (default_task) {
				stw_task_activate(default_task);
				server->active_task = default_task;
			}
		}
	}

	return true;

err_scene:
	/* Cleanup on failure */
err_allocator:
err_renderer:
err_backend:
	wlr_backend_destroy(server->backend);
err_display:
	wl_display_destroy(server->wl_display);
	return false;
}

void stw_server_run(struct stw_server *server) {
	/* Set environment for child processes */
	setenv("WAYLAND_DISPLAY", server->socket, true);
#if STW_HAS_XWAYLAND
	if (server->xwayland) {
		char display[16];
		snprintf(display, sizeof(display), ":%d",
			server->xwayland->display);
		setenv("DISPLAY", display, true);
	}
#endif

	/* Start the backend */
	if (!wlr_backend_start(server->backend)) {
		wlr_log(WLR_ERROR, "Failed to start backend");
		return;
	}

	wlr_log(WLR_INFO, "Running on WAYLAND_DISPLAY=%s", server->socket);
	server->running = true;

	/* Set up config file watching for live reload */
	if (server->config_path) {
		stw_config_watch(server->config, server);
	}

	/* Start session autosave (every 60 seconds) */
	stw_session_start_autosave(server, 60);

	/* Run the event loop */
	wl_display_run(server->wl_display);
}

void stw_server_finish(struct stw_server *server) {
	/* Save session before shutdown */
	stw_session_save(server);
	stw_session_stop_autosave(server);

	/* Clean up IPC */
	stw_ipc_finish(server);

	/* Clean up rules */
	stw_rules_finish(server);

	/* Destroy all tasks */
	struct stw_task *task, *task_tmp;
	wl_list_for_each_safe(task, task_tmp, &server->tasks, link) {
		stw_task_destroy(task);
	}

#if STW_HAS_XWAYLAND
	if (server->xwayland) {
		wlr_xwayland_destroy(server->xwayland);
	}
#endif

	wl_display_destroy_clients(server->wl_display);
	wlr_scene_node_destroy(&server->scene->tree.node);
	wlr_xcursor_manager_destroy(server->cursor_mgr);
	wlr_cursor_destroy(server->cursor);
	wlr_allocator_destroy(server->allocator);
	wlr_renderer_destroy(server->renderer);
	wlr_backend_destroy(server->backend);
	wl_display_destroy(server->wl_display);

	free(server->config_path);
	free(server->session_path);
}

/* ─── Focus management ─────────────────────────────────────────── */

void stw_server_focus_view(struct stw_server *server, struct stw_view *view) {
	if (!view || !view->mapped) {
		return;
	}

	struct wlr_surface *prev_surface =
		server->seat->keyboard_state.focused_surface;

	/* Get the surface to focus */
	struct wlr_surface *surface = NULL;
	switch (view->type) {
	case STW_VIEW_XDG_TOPLEVEL:
		surface = view->xdg_toplevel->base->surface;
		break;
#if STW_HAS_XWAYLAND
	case STW_VIEW_XWAYLAND:
		surface = view->xwayland_surface->surface;
		break;
#endif
	}

	if (!surface) {
		return;
	}

	if (prev_surface == surface) {
		/* Already focused */
		return;
	}

	/* Deactivate previously focused surface */
	if (prev_surface) {
		struct wlr_xdg_toplevel *prev_toplevel =
			wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
		if (prev_toplevel) {
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
		}
	}

	/* Raise the view in the scene */
	wlr_scene_node_raise_to_top(&view->scene_tree->node);

	/* Activate */
	switch (view->type) {
	case STW_VIEW_XDG_TOPLEVEL:
		wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);
		break;
#if STW_HAS_XWAYLAND
	case STW_VIEW_XWAYLAND:
		wlr_xwayland_surface_activate(view->xwayland_surface, true);
		break;
#endif
	}

	/* Send keyboard focus */
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard) {
		wlr_seat_keyboard_notify_enter(server->seat, surface,
			keyboard->keycodes, keyboard->num_keycodes,
			&keyboard->modifiers);
	}

	/* Update focus tracking */
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	view->last_focus_time = (uint64_t)now.tv_sec * 1000 +
		(uint64_t)now.tv_nsec / 1000000;

	/* Update task's last focused view */
	if (view->task) {
		view->task->last_focused = view;
	}
}

struct stw_view *stw_server_view_at(struct stw_server *server,
		double lx, double ly, struct wlr_surface **surface,
		double *sx, double *sy) {
	struct wlr_scene_node *node =
		wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}

	struct wlr_scene_buffer *scene_buffer =
		wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface) {
		return NULL;
	}

	*surface = scene_surface->surface;

	/* Walk up the scene tree to find the view */
	struct wlr_scene_tree *tree = node->parent;
	while (tree && !tree->node.data) {
		tree = tree->node.parent;
	}

	if (tree) {
		return tree->node.data;
	}

	return NULL;
}
