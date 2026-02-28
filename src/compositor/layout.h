/*
 * SingleThread - Task-Centric Wayland Compositor
 * layout.h - Layout engine (tiling + floating)
 */
#ifndef STW_LAYOUT_H
#define STW_LAYOUT_H

#include <stdbool.h>
#include <wlr/util/box.h>

struct stw_server;
struct stw_task;
struct stw_output;
struct stw_view;

/* ─── Layout types ─────────────────────────────────────────────── */
enum stw_layout_type {
	STW_LAYOUT_MASTER_STACK,
	STW_LAYOUT_BSP,
	STW_LAYOUT_FLOATING_ONLY,
};

/* ─── Focus direction ──────────────────────────────────────────── */
enum stw_direction {
	STW_DIR_LEFT,
	STW_DIR_RIGHT,
	STW_DIR_UP,
	STW_DIR_DOWN,
};

/* Arrange all tiled views in the active task on the given output */
void stw_layout_arrange(struct stw_server *server, struct stw_output *output);

/* Arrange all outputs */
void stw_layout_arrange_all(struct stw_server *server);

/* Master-stack layout */
void stw_layout_master_stack(struct stw_output *output,
	struct stw_view **views, int count,
	int master_count, double master_ratio,
	int gaps_inner, int gaps_outer);

/* BSP layout */
void stw_layout_bsp(struct stw_output *output,
	struct stw_view **views, int count,
	int gaps_inner, int gaps_outer);

/* Layout modifications */
void stw_layout_increase_master_ratio(struct stw_server *server, double delta);
void stw_layout_decrease_master_ratio(struct stw_server *server, double delta);
void stw_layout_increase_master_count(struct stw_server *server);
void stw_layout_decrease_master_count(struct stw_server *server);
void stw_layout_cycle(struct stw_server *server);

/* Focus navigation */
void stw_layout_focus_direction(struct stw_server *server,
	enum stw_direction dir);
void stw_layout_swap_direction(struct stw_server *server,
	enum stw_direction dir);
void stw_layout_zoom(struct stw_server *server);

/* Parse layout name */
enum stw_layout_type stw_layout_parse_type(const char *name);

#endif /* STW_LAYOUT_H */
