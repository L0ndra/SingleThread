/*
 * SingleThread - Task-Centric Wayland Compositor
 * layout.c - Layout engine (tiling + floating)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>

#include "layout.h"
#include "server.h"
#include "output.h"
#include "view.h"
#include "task.h"
#include "stw_config.h"

/* ─── Helpers ──────────────────────────────────────────────────── */

/* Collect tiled views for the active workspace of the active task */
static int collect_tiled_views(struct stw_server *server,
		struct stw_view **out, int max) {
	struct stw_task *task = server->active_task;
	if (!task) return 0;

	int count = 0;
	struct stw_view *view;
	wl_list_for_each(view, &task->views, task_link) {
		if (!view->mapped) continue;
		if (view->is_global) continue;
		if (view->state != STW_VIEW_TILED) continue;
		if (view->workspace_idx != task->active_workspace) continue;
		if (count >= max) break;
		out[count++] = view;
	}
	return count;
}

/* ─── Master-stack layout ──────────────────────────────────────── */

void stw_layout_master_stack(struct stw_output *output,
		struct stw_view **views, int count,
		int master_count, double master_ratio,
		int gaps_inner, int gaps_outer) {
	if (count == 0) return;

	struct wlr_box area;
	stw_output_get_usable_area(output, &area);

	/* Apply outer gaps */
	area.x += gaps_outer;
	area.y += gaps_outer;
	area.width -= 2 * gaps_outer;
	area.height -= 2 * gaps_outer;

	if (area.width <= 0 || area.height <= 0) return;

	if (master_count <= 0) master_count = 1;
	if (master_count > count) master_count = count;

	int stack_count = count - master_count;

	/* If only master windows, they fill the entire area */
	int master_width = (stack_count > 0) ?
		(int)(area.width * master_ratio) : area.width;
	int stack_width = area.width - master_width;
	if (stack_count > 0) {
		master_width -= gaps_inner / 2;
		stack_width -= gaps_inner / 2;
	}

	/* Layout master windows */
	int master_height = (area.height - (master_count - 1) * gaps_inner) /
		master_count;
	for (int i = 0; i < master_count; i++) {
		int y = area.y + i * (master_height + gaps_inner);
		stw_view_set_position(views[i], area.x, y);
		stw_view_set_size(views[i], master_width, master_height);
	}

	/* Layout stack windows */
	if (stack_count > 0) {
		int stack_x = area.x + master_width + gaps_inner;
		int stack_height = (area.height - (stack_count - 1) * gaps_inner) /
			stack_count;
		for (int i = 0; i < stack_count; i++) {
			int y = area.y + i * (stack_height + gaps_inner);
			stw_view_set_position(views[master_count + i], stack_x, y);
			stw_view_set_size(views[master_count + i],
				stack_width, stack_height);
		}
	}
}

/* ─── BSP layout ───────────────────────────────────────────────── */

static void bsp_recurse(struct stw_view **views, int start, int end,
		struct wlr_box area, int gaps_inner, bool horizontal) {
	if (start > end) return;

	if (start == end) {
		stw_view_set_position(views[start], area.x, area.y);
		stw_view_set_size(views[start], area.width, area.height);
		return;
	}

	int mid = start + (end - start) / 2;
	int left_count = mid - start + 1;
	int right_count = end - mid;
	double ratio = (double)left_count / (left_count + right_count);

	struct wlr_box left_area = area;
	struct wlr_box right_area = area;

	if (horizontal) {
		int split = (int)(area.width * ratio) - gaps_inner / 2;
		left_area.width = split;
		right_area.x = area.x + split + gaps_inner;
		right_area.width = area.width - split - gaps_inner;
	} else {
		int split = (int)(area.height * ratio) - gaps_inner / 2;
		left_area.height = split;
		right_area.y = area.y + split + gaps_inner;
		right_area.height = area.height - split - gaps_inner;
	}

	bsp_recurse(views, start, mid, left_area, gaps_inner, !horizontal);
	bsp_recurse(views, mid + 1, end, right_area, gaps_inner, !horizontal);
}

void stw_layout_bsp(struct stw_output *output,
		struct stw_view **views, int count,
		int gaps_inner, int gaps_outer) {
	if (count == 0) return;

	struct wlr_box area;
	stw_output_get_usable_area(output, &area);

	area.x += gaps_outer;
	area.y += gaps_outer;
	area.width -= 2 * gaps_outer;
	area.height -= 2 * gaps_outer;

	if (area.width <= 0 || area.height <= 0) return;

	bsp_recurse(views, 0, count - 1, area, gaps_inner,
		area.width >= area.height);
}

/* ─── Layout dispatch ──────────────────────────────────────────── */

void stw_layout_arrange(struct stw_server *server,
		struct stw_output *output) {
	if (!server->active_task) return;

	struct stw_view *tiled_views[256];
	int count = collect_tiled_views(server, tiled_views, 256);

	if (count == 0) return;

	/* Determine layout type */
	const char *layout_name = server->active_task->layout_name ?
		server->active_task->layout_name : server->config->default_layout;
	enum stw_layout_type layout = stw_layout_parse_type(layout_name);

	/* Get workspace layout parameters */
	int ws = server->active_task->active_workspace;
	int master_count = server->active_task->workspaces[ws].master_count;
	double master_ratio = server->active_task->workspaces[ws].master_ratio;

	switch (layout) {
	case STW_LAYOUT_MASTER_STACK:
		stw_layout_master_stack(output, tiled_views, count,
			master_count, master_ratio,
			server->config->gaps_inner,
			server->config->gaps_outer);
		break;

	case STW_LAYOUT_BSP:
		stw_layout_bsp(output, tiled_views, count,
			server->config->gaps_inner,
			server->config->gaps_outer);
		break;

	case STW_LAYOUT_FLOATING_ONLY:
		/* No automatic tiling */
		break;
	}

	/* Handle fullscreen views */
	struct stw_view *view;
	wl_list_for_each(view, &server->active_task->views, task_link) {
		if (!view->mapped) continue;
		if (view->state != STW_VIEW_FULLSCREEN) continue;
		if (view->workspace_idx != server->active_task->active_workspace)
			continue;

		struct wlr_box full_area;
		stw_output_get_usable_area(output, &full_area);
		/* For fullscreen, use the entire output */
		full_area.x -= server->config->gaps_outer;
		full_area.y -= server->config->gaps_outer;
		full_area.width += 2 * server->config->gaps_outer;
		full_area.height += 2 * server->config->gaps_outer;
		stw_view_set_position(view, full_area.x, full_area.y);
		stw_view_set_size(view, full_area.width, full_area.height);
	}
}

void stw_layout_arrange_all(struct stw_server *server) {
	struct stw_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		stw_layout_arrange(server, output);
	}
}

/* ─── Layout modifications ─────────────────────────────────────── */

void stw_layout_increase_master_ratio(struct stw_server *server,
		double delta) {
	if (!server->active_task) return;
	int ws = server->active_task->active_workspace;
	double *ratio = &server->active_task->workspaces[ws].master_ratio;
	*ratio += delta;
	if (*ratio > 0.9) *ratio = 0.9;
	stw_layout_arrange_all(server);
}

void stw_layout_decrease_master_ratio(struct stw_server *server,
		double delta) {
	if (!server->active_task) return;
	int ws = server->active_task->active_workspace;
	double *ratio = &server->active_task->workspaces[ws].master_ratio;
	*ratio -= delta;
	if (*ratio < 0.1) *ratio = 0.1;
	stw_layout_arrange_all(server);
}

void stw_layout_increase_master_count(struct stw_server *server) {
	if (!server->active_task) return;
	int ws = server->active_task->active_workspace;
	server->active_task->workspaces[ws].master_count++;
	stw_layout_arrange_all(server);
}

void stw_layout_decrease_master_count(struct stw_server *server) {
	if (!server->active_task) return;
	int ws = server->active_task->active_workspace;
	int *mc = &server->active_task->workspaces[ws].master_count;
	if (*mc > 1) {
		(*mc)--;
		stw_layout_arrange_all(server);
	}
}

void stw_layout_cycle(struct stw_server *server) {
	if (!server->active_task) return;

	const char *current = server->active_task->layout_name ?
		server->active_task->layout_name : server->config->default_layout;
	enum stw_layout_type type = stw_layout_parse_type(current);

	/* Cycle: master-stack -> bsp -> floating -> master-stack */
	const char *next;
	switch (type) {
	case STW_LAYOUT_MASTER_STACK:
		next = "bsp";
		break;
	case STW_LAYOUT_BSP:
		next = "floating";
		break;
	case STW_LAYOUT_FLOATING_ONLY:
	default:
		next = "master-stack";
		break;
	}

	free(server->active_task->layout_name);
	server->active_task->layout_name = strdup(next);
	wlr_log(WLR_INFO, "Layout cycled to: %s", next);
	stw_layout_arrange_all(server);
}

/* ─── Focus navigation ─────────────────────────────────────────── */

void stw_layout_focus_direction(struct stw_server *server,
		enum stw_direction dir) {
	if (!server->active_task) return;

	struct stw_view *focused = server->active_task->last_focused;
	if (!focused) return;

	struct wlr_box focused_box;
	stw_view_get_geometry(focused, &focused_box);
	int fx = focused_box.x + focused_box.width / 2;
	int fy = focused_box.y + focused_box.height / 2;

	struct stw_view *best = NULL;
	int best_dist = INT32_MAX;

	struct stw_view *view;
	wl_list_for_each(view, &server->active_task->views, task_link) {
		if (view == focused || !view->mapped) continue;
		if (view->workspace_idx != server->active_task->active_workspace)
			continue;

		struct wlr_box box;
		stw_view_get_geometry(view, &box);
		int vx = box.x + box.width / 2;
		int vy = box.y + box.height / 2;

		/* Check direction */
		bool valid = false;
		switch (dir) {
		case STW_DIR_LEFT:  valid = (vx < fx); break;
		case STW_DIR_RIGHT: valid = (vx > fx); break;
		case STW_DIR_UP:    valid = (vy < fy); break;
		case STW_DIR_DOWN:  valid = (vy > fy); break;
		}
		if (!valid) continue;

		int dist = abs(vx - fx) + abs(vy - fy);
		if (dist < best_dist) {
			best_dist = dist;
			best = view;
		}
	}

	if (best) {
		stw_server_focus_view(server, best);
	}
}

void stw_layout_swap_direction(struct stw_server *server,
		enum stw_direction dir) {
	if (!server->active_task) return;

	struct stw_view *focused = server->active_task->last_focused;
	if (!focused) return;

	/* Find target view in direction (reuse focus logic) */
	struct wlr_box focused_box;
	stw_view_get_geometry(focused, &focused_box);
	int fx = focused_box.x + focused_box.width / 2;
	int fy = focused_box.y + focused_box.height / 2;

	struct stw_view *target = NULL;
	int best_dist = INT32_MAX;

	struct stw_view *view;
	wl_list_for_each(view, &server->active_task->views, task_link) {
		if (view == focused || !view->mapped) continue;
		if (view->state != STW_VIEW_TILED) continue;
		if (view->workspace_idx != server->active_task->active_workspace)
			continue;

		struct wlr_box box;
		stw_view_get_geometry(view, &box);
		int vx = box.x + box.width / 2;
		int vy = box.y + box.height / 2;

		bool valid = false;
		switch (dir) {
		case STW_DIR_LEFT:  valid = (vx < fx); break;
		case STW_DIR_RIGHT: valid = (vx > fx); break;
		case STW_DIR_UP:    valid = (vy < fy); break;
		case STW_DIR_DOWN:  valid = (vy > fy); break;
		}
		if (!valid) continue;

		int dist = abs(vx - fx) + abs(vy - fy);
		if (dist < best_dist) {
			best_dist = dist;
			target = view;
		}
	}

	if (target) {
		/* Swap positions in the task's view list */
		struct wl_list tmp;
		wl_list_init(&tmp);

		/* Save links */
		struct wl_list *focused_prev = focused->task_link.prev;
		struct wl_list *focused_next = focused->task_link.next;
		struct wl_list *target_prev = target->task_link.prev;
		struct wl_list *target_next = target->task_link.next;

		/* Handle adjacent nodes */
		if (focused_next == &target->task_link) {
			wl_list_remove(&focused->task_link);
			wl_list_insert(&target->task_link, &focused->task_link);
		} else if (target_next == &focused->task_link) {
			wl_list_remove(&target->task_link);
			wl_list_insert(&focused->task_link, &target->task_link);
		} else {
			wl_list_remove(&focused->task_link);
			wl_list_remove(&target->task_link);
			/* Re-insert at each other's positions */
			focused->task_link.prev = target_prev;
			focused->task_link.next = target_next;
			target_prev->next = &focused->task_link;
			target_next->prev = &focused->task_link;

			target->task_link.prev = focused_prev;
			target->task_link.next = focused_next;
			focused_prev->next = &target->task_link;
			focused_next->prev = &target->task_link;
		}

		stw_layout_arrange_all(server);
	}
}

void stw_layout_zoom(struct stw_server *server) {
	if (!server->active_task) return;

	struct stw_view *focused = server->active_task->last_focused;
	if (!focused || focused->state != STW_VIEW_TILED) return;

	/* Move focused view to front of list (make it master) */
	wl_list_remove(&focused->task_link);
	wl_list_insert(&server->active_task->views, &focused->task_link);

	stw_layout_arrange_all(server);
}

/* ─── Layout type parsing ──────────────────────────────────────── */

enum stw_layout_type stw_layout_parse_type(const char *name) {
	if (!name) return STW_LAYOUT_MASTER_STACK;
	if (strcmp(name, "bsp") == 0) return STW_LAYOUT_BSP;
	if (strcmp(name, "floating") == 0) return STW_LAYOUT_FLOATING_ONLY;
	return STW_LAYOUT_MASTER_STACK;
}
