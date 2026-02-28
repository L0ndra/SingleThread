/*
 * SingleThread - Task-Centric Wayland Compositor
 * output.h - Output (monitor) management
 */
#ifndef STW_OUTPUT_H
#define STW_OUTPUT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>

struct stw_server;

/* ─── Output (monitor/display) ─────────────────────────────────── */
struct stw_output {
	struct wl_list link;           /* stw_server.outputs */
	struct stw_server *server;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;

	/* Configuration */
	float scale;
	enum wl_output_transform transform;

	/* Usable area (excluding panels/layer surfaces) */
	struct wlr_box usable_area;

	/* Listeners */
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

/* Output management */
void stw_output_handle_new(struct wl_listener *listener, void *data);
void stw_output_destroy(struct stw_output *output);

/* Output queries */
struct stw_output *stw_output_at(struct stw_server *server, double x, double y);
struct stw_output *stw_output_get_focused(struct stw_server *server);
void stw_output_get_usable_area(struct stw_output *output, struct wlr_box *box);

#endif /* STW_OUTPUT_H */
