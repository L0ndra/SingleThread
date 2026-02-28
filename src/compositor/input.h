/*
 * SingleThread - Task-Centric Wayland Compositor
 * input.h - Input device handling (keyboard, pointer, touch)
 */
#ifndef STW_INPUT_H
#define STW_INPUT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>

struct stw_server;

/* ─── Keyboard ─────────────────────────────────────────────────── */
struct stw_keyboard {
	struct wl_list link;               /* stw_server.keyboards */
	struct stw_server *server;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

/* Input setup */
void stw_input_handle_new(struct wl_listener *listener, void *data);

/* Keyboard */
void stw_keyboard_setup(struct stw_server *server,
	struct wlr_input_device *device);

/* Cursor/pointer callbacks */
void stw_cursor_motion(struct wl_listener *listener, void *data);
void stw_cursor_motion_absolute(struct wl_listener *listener, void *data);
void stw_cursor_button(struct wl_listener *listener, void *data);
void stw_cursor_axis(struct wl_listener *listener, void *data);
void stw_cursor_frame(struct wl_listener *listener, void *data);
void stw_cursor_request_set(struct wl_listener *listener, void *data);

/* Interactive operations */
void stw_cursor_begin_move(struct stw_server *server, struct stw_view *view);
void stw_cursor_begin_resize(struct stw_server *server, struct stw_view *view,
	uint32_t edges);

#endif /* STW_INPUT_H */
