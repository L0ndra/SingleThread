/*
 * SingleThread - Task-Centric Wayland Compositor
 * input.c - Input device handling
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>

#include "input.h"
#include "server.h"
#include "view.h"
#include "keybind.h"
#include "layout.h"

/* ─── Keyboard handling ────────────────────────────────────────── */

static void keyboard_modifiers(struct wl_listener *listener, void *data) {
	struct stw_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	(void)data;

	wlr_seat_set_keyboard(keyboard->server->seat,
		keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->wlr_keyboard->modifiers);
}

static void keyboard_key(struct wl_listener *listener, void *data) {
	struct stw_keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
	struct wlr_keyboard_key_event *event = data;
	struct stw_server *server = keyboard->server;

	/* Translate key to keysym */
	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
		keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

	bool handled = false;

	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms; i++) {
			handled = stw_keybind_handle(server, modifiers, syms[i]);
			if (handled) break;
		}
	}

	/* Pass unhandled keys to the focused client */
	if (!handled) {
		wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(server->seat,
			event->time_msec, event->keycode, event->state);
	}
}

static void keyboard_destroy(struct wl_listener *listener, void *data) {
	struct stw_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	(void)data;

	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

void stw_keyboard_setup(struct stw_server *server,
		struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct stw_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (!keyboard) return;

	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	/* Set up XKB keymap */
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (keymap) {
		wlr_keyboard_set_keymap(wlr_keyboard, keymap);
		xkb_keymap_unref(keymap);
	}
	xkb_context_unref(context);

	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	/* Set up listeners */
	keyboard->modifiers.notify = keyboard_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

	keyboard->key.notify = keyboard_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

	keyboard->destroy.notify = keyboard_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, wlr_keyboard);

	wl_list_insert(&server->keyboards, &keyboard->link);

	wlr_log(WLR_INFO, "Keyboard added: %s", device->name);
}

/* ─── New input device ─────────────────────────────────────────── */

void stw_input_handle_new(struct wl_listener *listener, void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		stw_keyboard_setup(server, device);
		break;

	case WLR_INPUT_DEVICE_POINTER:
		wlr_cursor_attach_input_device(server->cursor, device);
		wlr_log(WLR_INFO, "Pointer added: %s", device->name);
		break;

	case WLR_INPUT_DEVICE_TOUCH:
		wlr_cursor_attach_input_device(server->cursor, device);
		wlr_log(WLR_INFO, "Touch device added: %s", device->name);
		break;

	default:
		wlr_log(WLR_DEBUG, "Input device added: type=%d name=%s",
			device->type, device->name);
		break;
	}

	/* Update seat capabilities */
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

/* ─── Cursor/pointer event handlers ───────────────────────────── */

static void process_cursor_motion(struct stw_server *server, uint32_t time) {
	if (server->cursor_mode == STW_CURSOR_MOVE) {
		/* Interactive move */
		struct stw_view *view = server->grabbed_view;
		if (view) {
			stw_view_set_position(view,
				(int)(server->cursor->x - server->grab_x),
				(int)(server->cursor->y - server->grab_y));
		}
		return;
	}

	if (server->cursor_mode == STW_CURSOR_RESIZE) {
		/* Interactive resize */
		struct stw_view *view = server->grabbed_view;
		if (view) {
			double border_x = server->cursor->x - server->grab_x;
			double border_y = server->cursor->y - server->grab_y;
			int new_left = server->grab_geobox.x;
			int new_right = server->grab_geobox.x +
				server->grab_geobox.width;
			int new_top = server->grab_geobox.y;
			int new_bottom = server->grab_geobox.y +
				server->grab_geobox.height;

			if (server->resize_edges & WLR_EDGE_TOP) {
				new_top = (int)border_y;
				if (new_top >= new_bottom)
					new_top = new_bottom - 1;
			} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
				new_bottom = (int)border_y;
				if (new_bottom <= new_top)
					new_bottom = new_top + 1;
			}
			if (server->resize_edges & WLR_EDGE_LEFT) {
				new_left = (int)border_x;
				if (new_left >= new_right)
					new_left = new_right - 1;
			} else if (server->resize_edges & WLR_EDGE_RIGHT) {
				new_right = (int)border_x;
				if (new_right <= new_left)
					new_right = new_left + 1;
			}

			stw_view_set_position(view, new_left, new_top);
			stw_view_set_size(view,
				new_right - new_left, new_bottom - new_top);
		}
		return;
	}

	/* Passthrough: find surface under cursor */
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct stw_view *view = stw_server_view_at(server,
		server->cursor->x, server->cursor->y, &surface, &sx, &sy);

	if (!view) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
			"default");
	}

	if (surface) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(server->seat);
	}
}

void stw_cursor_motion(struct wl_listener *listener, void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;

	wlr_cursor_move(server->cursor, &event->pointer->base,
		event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

void stw_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;

	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base,
		event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

void stw_cursor_button(struct wl_listener *listener, void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	wlr_seat_pointer_notify_button(server->seat,
		event->time_msec, event->button, event->state);

	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		/* End interactive move/resize */
		if (server->cursor_mode != STW_CURSOR_PASSTHROUGH) {
			server->cursor_mode = STW_CURSOR_PASSTHROUGH;
			server->grabbed_view = NULL;
			stw_layout_arrange_all(server);
		}
		return;
	}

	/* Focus view under cursor on click */
	double sx, sy;
	struct wlr_surface *surface;
	struct stw_view *view = stw_server_view_at(server,
		server->cursor->x, server->cursor->y, &surface, &sx, &sy);

	if (view) {
		stw_server_focus_view(server, view);
	}
}

void stw_cursor_axis(struct wl_listener *listener, void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;

	wlr_seat_pointer_notify_axis(server->seat,
		event->time_msec, event->orientation,
		event->delta, event->delta_discrete, event->source,
		event->relative_direction);
}

void stw_cursor_frame(struct wl_listener *listener, void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, cursor_frame);
	(void)data;
	wlr_seat_pointer_notify_frame(server->seat);
}

void stw_cursor_request_set(struct wl_listener *listener, void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, request_set_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;

	struct wlr_seat_client *focused_client =
		server->seat->pointer_state.focused_client;
	if (focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface,
			event->hotspot_x, event->hotspot_y);
	}
}

/* ─── Interactive operations ───────────────────────────────────── */

void stw_cursor_begin_move(struct stw_server *server, struct stw_view *view) {
	if (!view) return;

	/* Ensure view is floating for move */
	if (view->state == STW_VIEW_TILED) {
		stw_view_set_floating(view, true);
	}

	server->grabbed_view = view;
	server->cursor_mode = STW_CURSOR_MOVE;

	struct wlr_box geo;
	stw_view_get_geometry(view, &geo);
	server->grab_x = server->cursor->x - geo.x;
	server->grab_y = server->cursor->y - geo.y;
}

void stw_cursor_begin_resize(struct stw_server *server, struct stw_view *view,
		uint32_t edges) {
	if (!view) return;

	/* Ensure view is floating for resize */
	if (view->state == STW_VIEW_TILED) {
		stw_view_set_floating(view, true);
	}

	server->grabbed_view = view;
	server->cursor_mode = STW_CURSOR_RESIZE;
	server->resize_edges = edges;

	struct wlr_box geo;
	stw_view_get_geometry(view, &geo);
	server->grab_geobox = geo;

	double border_x = geo.x + ((edges & WLR_EDGE_RIGHT) ? geo.width : 0);
	double border_y = geo.y + ((edges & WLR_EDGE_BOTTOM) ? geo.height : 0);
	server->grab_x = server->cursor->x - border_x;
	server->grab_y = server->cursor->y - border_y;
}
