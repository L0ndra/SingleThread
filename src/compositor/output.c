/*
 * SingleThread - Task-Centric Wayland Compositor
 * output.c - Output (monitor) management
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "output.h"
#include "server.h"
#include "layout.h"
#include "stw_config.h"

static void output_frame(struct wl_listener *listener, void *data) {
	struct stw_output *output = wl_container_of(listener, output, frame);
	(void)data;

	struct wlr_scene_output *scene_output = output->scene_output;
	if (!scene_output) return;

	wlr_scene_output_commit(scene_output, NULL);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
	struct stw_output *output =
		wl_container_of(listener, output, request_state);
	struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy_handler(struct wl_listener *listener, void *data) {
	struct stw_output *output = wl_container_of(listener, output, destroy);
	(void)data;

	wlr_log(WLR_INFO, "Output '%s' disconnected", output->wlr_output->name);

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);

	free(output);
}

/* Apply configuration for a specific output */
static void apply_output_config(struct stw_output *output,
		struct stw_config *config) {
	struct stw_output_config *oc;
	wl_list_for_each(oc, &config->output_configs, link) {
		if (strcmp(oc->name, output->wlr_output->name) == 0) {
			wlr_log(WLR_INFO, "Applying config for output '%s'", oc->name);

			struct wlr_output_state state;
			wlr_output_state_init(&state);

			if (oc->scale > 0) {
				wlr_output_state_set_scale(&state, oc->scale);
				output->scale = oc->scale;
			}

			if (oc->transform >= 0) {
				wlr_output_state_set_transform(&state, oc->transform);
			}

			if (oc->mode && strcmp(oc->mode, "preferred") != 0) {
				/* Parse "WxH@R" format */
				int w, h;
				float rate = 0;
				if (sscanf(oc->mode, "%dx%d@%f", &w, &h, &rate) >= 2) {
					struct wlr_output_mode *mode;
					wl_list_for_each(mode,
							&output->wlr_output->modes, link) {
						if (mode->width == w && mode->height == h &&
								(rate == 0 ||
								 abs(mode->refresh - (int)(rate * 1000)) < 500)) {
							wlr_output_state_set_mode(&state, mode);
							break;
						}
					}
				}
			}

			wlr_output_commit_state(output->wlr_output, &state);
			wlr_output_state_finish(&state);
			return;
		}
	}
}

void stw_output_handle_new(struct wl_listener *listener, void *data) {
	struct stw_server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	wlr_log(WLR_INFO, "New output: %s (%s %s)",
		wlr_output->name,
		wlr_output->make ? wlr_output->make : "unknown",
		wlr_output->model ? wlr_output->model : "unknown");

	/* Initialize output with preferred mode */
	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode) {
		wlr_output_state_set_mode(&state, mode);
	}

	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	/* Create our output wrapper */
	struct stw_output *output = calloc(1, sizeof(*output));
	if (!output) {
		wlr_log(WLR_ERROR, "Failed to allocate output");
		return;
	}

	output->server = server;
	output->wlr_output = wlr_output;
	output->scale = wlr_output->scale;

	/* Set up listeners */
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state,
		&output->request_state);

	output->destroy.notify = output_destroy_handler;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	wl_list_insert(&server->outputs, &output->link);

	/* Add to output layout */
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout,
		l_output, output->scene_output);

	/* Apply per-output config */
	apply_output_config(output, server->config);

	/* Calculate usable area */
	stw_output_get_usable_area(output, &output->usable_area);

	/* Re-layout to account for new output */
	stw_layout_arrange_all(server);
}

void stw_output_destroy(struct stw_output *output) {
	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
}

/* ─── Output queries ───────────────────────────────────────────── */

struct stw_output *stw_output_at(struct stw_server *server,
		double x, double y) {
	struct wlr_output *wlr_output =
		wlr_output_layout_output_at(server->output_layout, x, y);
	if (!wlr_output) return NULL;

	struct stw_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (output->wlr_output == wlr_output) {
			return output;
		}
	}
	return NULL;
}

struct stw_output *stw_output_get_focused(struct stw_server *server) {
	/* Return the output under the cursor */
	return stw_output_at(server,
		server->cursor->x, server->cursor->y);
}

void stw_output_get_usable_area(struct stw_output *output,
		struct wlr_box *box) {
	struct wlr_output_layout_output *l_output =
		wlr_output_layout_get(output->server->output_layout,
			output->wlr_output);
	if (!l_output) {
		box->x = 0;
		box->y = 0;
		box->width = 0;
		box->height = 0;
		return;
	}

	box->x = l_output->x;
	box->y = l_output->y;
	box->width = output->wlr_output->width;
	box->height = output->wlr_output->height;

	/* TODO: subtract layer shell exclusive zones */
}
